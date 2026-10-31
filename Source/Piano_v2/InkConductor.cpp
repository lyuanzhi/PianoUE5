// Fill out your copyright notice in the Description page of Project Settings.


#include "InkConductor.h"
#include "Algo/AllOf.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Engine/FontFace.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Fonts/CompositeFont.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InkCalligraphy.h"
#include "InkLandscape.h"
#include "InputKeyEventArgs.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

DEFINE_LOG_CATEGORY_STATIC(LogInk, Log, All);

namespace
{
	const FName IndexName(TEXT("Index"));
	const FName PauseName(TEXT("Sound Pause"));
	const FName KeysName(TEXT("Keys Array"));
	const FName VelocitiesName(TEXT("Velocity Array"));
	const FName SongNameName(TEXT("Current Song Name"));
	const FName SongNamesName(TEXT("Song Names"));
	constexpr int32 NumKeys = 88;
	// impact time of a pool slot that is not in use
	constexpr double Idle = -1000.0;
}

AInkConductor::AInkConductor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ripple(TEXT("/Game/InkWash/Materials/M_InkRipple"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SealStone(TEXT("/Game/InkWash/Materials/M_InkSeal"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
	static ConstructorHelpers::FObjectFinder<UFontFace> Brush(TEXT("/Game/InkWash/Fonts/MaShanZheng-Regular"));
	RippleMaterial = Ripple.Object;
	SealMaterial = SealStone.Object;
	RippleMesh = Plane.Object;
	BrushFace = Brush.Object;
}

void AInkConductor::BeginPlay()
{
	Super::BeginPlay();

	if (!FindPiano())
	{
		UE_LOG(LogInk, Warning, TEXT("InkConductor: no piano with \"Index\" and \"Keys Array\" in the level, ink stays off"));
		SetActorTickEnabled(false);
		return;
	}
	CacheKeys();
	CreatePools();
	if (BrushFace)
	{
		TSharedRef<FCompositeFont> Composite = MakeShared<FCompositeFont>();
		Composite->DefaultTypeface.Fonts.Emplace_GetRef(TEXT("Regular")).Font = FFontData(BrushFace);
		BrushComposite = Composite;
	}
	PrewarmShaders();

	FActorSpawnParameters Spawn;
	Spawn.Owner = this;
	Calligraphy = GetWorld()->SpawnActor<AInkCalligraphy>(AInkCalligraphy::StaticClass(), FTransform::Identity, Spawn);
	// deferred, so the landscape knows where the water is when it builds its peaks
	Landscape = GetWorld()->SpawnActorDeferred<AInkLandscape>(AInkLandscape::StaticClass(), FTransform::Identity, this);
	Landscape->WaterZ = WaterZ;
	Landscape->FinishSpawning(FTransform::Identity);
	Landscape->Reflect(Piano);
	FinaleCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Spawn);
	FinaleCamera->GetCameraComponent()->SetConstraintAspectRatio(false);

	if (bAutoPlayOnStart)
	{
		FTimerHandle Press;
		FTimerHandle Release;
		GetWorldTimerManager().SetTimer(Press, FTimerDelegate::CreateWeakLambda(this, [this]() { SendPlayKey(IE_Pressed); }), 1.f, false);
		GetWorldTimerManager().SetTimer(Release, FTimerDelegate::CreateWeakLambda(this, [this]() { SendPlayKey(IE_Released); }), 1.1f, false);
	}
}

void AInkConductor::SendPlayKey(EInputEvent Event) const
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	UGameViewportClient* ViewportClient = GetWorld()->GetGameViewport();
	if (Player && ViewportClient)
	{
		// device 0 is the keyboard of the default input device mapping
		Player->InputKey(FInputKeyEventArgs(ViewportClient->Viewport, FInputDeviceId::CreateFromInternalId(0), EKeys::Tab, Event, FPlatformTime::Cycles64()));
	}
}

bool AInkConductor::FindPiano()
{
	auto IsPiano = [](const AActor* Actor)
	{
		return Actor && FindFProperty<FIntProperty>(Actor->GetClass(), IndexName) && FindFProperty<FArrayProperty>(Actor->GetClass(), KeysName);
	};
	if (!IsPiano(Piano))
	{
		Piano = nullptr;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (IsPiano(*It))
			{
				Piano = *It;
				break;
			}
		}
	}
	if (!Piano)
	{
		return false;
	}
	UClass* PianoClass = Piano->GetClass();
	IndexProp = FindFProperty<FIntProperty>(PianoClass, IndexName);
	PauseProp = FindFProperty<FBoolProperty>(PianoClass, PauseName);
	KeysProp = FindFProperty<FArrayProperty>(PianoClass, KeysName);
	VelocitiesProp = FindFProperty<FArrayProperty>(PianoClass, VelocitiesName);
	SongNameProp = FindFProperty<FStrProperty>(PianoClass, SongNameName);
	SongNamesProp = FindFProperty<FArrayProperty>(PianoClass, SongNamesName);
	return true;
}

void AInkConductor::CacheKeys()
{
	// the water lies on the ground the piano stands on; the rings start from just beyond its feet
	const FBox Box = Piano->GetComponentsBoundingBox();
	FCollisionQueryParams TraceParams;
	TraceParams.AddIgnoredActor(Piano);
	FHitResult Hit;
	const FVector Center = Box.GetCenter();
	WaterZ = GetWorld()->LineTraceSingleByChannel(Hit, Center, Center - FVector(0.f, 0.f, 10000.f), ECC_Visibility, TraceParams) ? Hit.ImpactPoint.Z : Box.Min.Z;
	RippleCenter = FVector(Center.X, Center.Y, WaterZ);
	RippleInner = FMath::Max(Box.GetExtent().X, Box.GetExtent().Y) * 0.85f;
}

void AInkConductor::CreatePools()
{
	auto CreateMesh = [this](UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(this);
		Comp->SetStaticMesh(Mesh);
		Comp->SetMobility(EComponentMobility::Movable);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetVisibility(false);
		Comp->SetupAttachment(RootComponent);
		Comp->RegisterComponent();
		Comp->SetMaterial(0, Material);
		return Comp;
	};

	for (int32 i = 0; i < RipplePoolSize; ++i)
	{
		UStaticMeshComponent* Ripple = CreateMesh(RippleMesh, RippleMaterial);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(RippleMaterial, Ripple);
		MID->SetScalarParameterValue(TEXT("Inner"), RippleInner);
		Ripple->SetMaterial(0, MID);
		Ripples.Add(Ripple);
		RippleMIDs.Add(MID);
	}
	RippleEnd.Init(Idle, Ripples.Num());
}

void AInkConductor::PrewarmShaders()
{
	// Show one ring with a long-past impact time for the first half second: it draws nothing, but its shader
	// gets compiled before the first note instead of stalling it.
	if (!Ripples.IsEmpty())
	{
		Ripples[0]->SetWorldLocation(RippleCenter);
		Ripples[0]->SetVisibility(true);
		RippleEnd[0] = 0.5;
	}
}

bool AInkConductor::ReadRow(const FArrayProperty* RowsProp, int32 Row, TArray<int32>& Out) const
{
	Out.Reset();
	const FStructProperty* RowProp = RowsProp ? CastField<FStructProperty>(RowsProp->Inner) : nullptr;
	if (!RowProp)
	{
		return false;
	}
	FScriptArrayHelper Rows(RowsProp, RowsProp->ContainerPtrToValuePtr<void>(Piano.Get()));
	if (!Rows.IsValidIndex(Row))
	{
		return false;
	}
	// KeyStruct is a user-defined struct whose one int array has a generated name, so take the first such member
	for (TFieldIterator<FArrayProperty> It(RowProp->Struct); It; ++It)
	{
		if (!It->Inner->IsA<FIntProperty>())
		{
			continue;
		}
		FScriptArrayHelper Values(*It, It->ContainerPtrToValuePtr<void>(Rows.GetRawPtr(Row)));
		for (int32 i = 0; i < Values.Num(); ++i)
		{
			Out.Add(*reinterpret_cast<const int32*>(Values.GetRawPtr(i)));
		}
		return true;
	}
	return false;
}

int32 AInkConductor::RowCount() const
{
	return FScriptArrayHelper(KeysProp, KeysProp->ContainerPtrToValuePtr<void>(Piano.Get())).Num();
}

void AInkConductor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsValid(Piano))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	FollowPlayback(Now);
	RetireExpired(Now);
	UpdateFinale(Now);
	// the piano creates its widgets whenever it likes, so keep an eye out for new ones
	if (Now >= NextHudCheck)
	{
		NextHudCheck = Now + 0.5;
		RestyleHud();
	}
}

void AInkConductor::FollowPlayback(double Now)
{
	const int32 Index = IndexProp->GetPropertyValue_InContainer(Piano.Get());
	const bool bPaused = PauseProp && PauseProp->GetPropertyValue_InContainer(Piano.Get());
	const int32 Rows = RowCount();
	if (LastIndex == INDEX_NONE)
	{
		LastIndex = Index;
		LastRowCount = Rows;
		ReadSong();
		return;
	}

	if (Index != LastIndex || Rows != LastRowCount)
	{
		// Index moving on by one means the piano has just played row Index - 1: that moment is our clock.
		// A reset to 0 or a new song stops following until a row actually sounds again.
		const bool bWasFollowing = bFollowing;
		bFollowing = Index == LastIndex + 1 && Rows == LastRowCount;
		if (Index < LastIndex || Rows != LastRowCount)
		{
			// a new song, or the same one from the top: a fresh sheet of paper
			ResetPainting();
		}
		if (Rows != LastRowCount)
		{
			ReadSong();
		}
		TArray<int32> PlayedRow;
		if (bFollowing && bWasFollowing && ReadRow(KeysProp, Index - 1, PlayedRow) && PlayedRow.Num() > 0)
		{
			// clamp out hitches so one long frame does not shift every prediction after it
			const double Lag = FMath::Clamp(Now - RowPlayedTime - PlayedRow[0] / 1000.0, 0.0, 0.1);
			RowLag = FMath::Lerp(RowLag, Lag, 0.3);
		}
		LastPlayedRow = Index - 1;
		RowPlayedTime = Now;
		if (!bFollowing)
		{
			ScheduledUpTo = LastPlayedRow;
		}
		LastIndex = Index;
		LastRowCount = Rows;
		UE_LOG(LogInk, Verbose, TEXT("row %d played at %.3f (following: %d, lag %.1f ms)"), LastPlayedRow, Now, bFollowing, RowLag * 1000.0);
		if (bFollowing && Index == Rows && Rows > 1)
		{
			// the last row has sounded: let it ring, then show the painting
			FinaleAt = Now + FinaleDelay;
			UE_LOG(LogInk, Display, TEXT("song finished at %.2f, finale at %.2f"), Now, FinaleAt);
		}
	}
	if (!bFollowing || bPaused)
	{
		return;
	}

	// Every row starts with the delay before it, so the time each coming row sounds is known in advance.
	// Hand on the notes of every row that sounds within the next LookAhead seconds.
	TArray<int32> Row;
	double RowTime = RowPlayedTime;
	for (int32 Next = LastPlayedRow + 1; Next < Rows; ++Next)
	{
		if (!ReadRow(KeysProp, Next, Row) || Row.Num() == 0)
		{
			break;
		}
		RowTime += Row[0] / 1000.0 + RowLag;
		if (Next == LastPlayedRow + 1 && RowTime < Now - 1.0)
		{
			// the next row is long overdue: the piano stopped without telling us
			bFollowing = false;
			break;
		}
		if (RowTime - Now > LookAhead)
		{
			break;
		}
		if (Next > ScheduledUpTo)
		{
			SpawnRow(Next, RowTime);
			ScheduledUpTo = Next;
		}
	}
}

void AInkConductor::SpawnRow(int32 RowIndex, double ImpactTime)
{
	TArray<int32> Keys;
	TArray<int32> Velocities;
	ReadRow(KeysProp, RowIndex, Keys);
	ReadRow(VelocitiesProp, RowIndex, Velocities);

	int32 TopKey = INDEX_NONE;
	float TopWeight = 0.f;
	const float Progress = RowIndex / float(FMath::Max(RowCount() - 1, 1));
	float Loudest = 0.f;
	// element 0 of a row is its delay; the notes follow
	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		const int32 Key = Keys[i];
		if (Key < 0 || Key >= NumKeys)
		{
			continue;
		}
		const float Velocity = Velocities.IsValidIndex(i) ? Velocities[i] : 64.f;
		// louder notes carry more ink, and low notes more than high ones
		const float Weight = FMath::Min(FMath::Clamp((Velocity - 20.f) / 90.f, 0.15f, 1.f) * FMath::Lerp(1.05f, 0.7f, Key / float(NumKeys - 1)), 1.f);
		Landscape->AddNote(ImpactTime, Progress, Key, Weight);
		Loudest = FMath::Max(Loudest, Weight);
		if (Key > TopKey)
		{
			TopKey = Key;
			TopWeight = Weight;
		}
	}
	// one ring for a row, and no more than a few a second, so a fast run does not crowd the water
	if (Loudest > 0.f && ImpactTime - LastRippleTime >= MinRippleInterval)
	{
		SpawnRipple(ImpactTime, Loudest);
		LastRippleTime = ImpactTime;
	}
	if (TopKey != INDEX_NONE)
	{
		// the top note of each row is the melody the brush writes
		Calligraphy->AddNote(ImpactTime, TopKey, TopWeight);
	}
	UE_LOG(LogInk, Verbose, TEXT("row %d: %d notes, lands at %.3f"), RowIndex, Keys.Num() - 1, ImpactTime);
}

void AInkConductor::SpawnRipple(double ImpactTime, float Weight)
{
	if (Ripples.IsEmpty())
	{
		return;
	}
	const int32 Slot = NextRipple++ % Ripples.Num();
	UStaticMeshComponent* Ripple = Ripples[Slot];
	// lying a hair above the water, just big enough for the outermost ring (the plane mesh is 100 units across)
	const float Reach = FMath::Lerp(RippleReach.X, RippleReach.Y, Weight);
	const float Half = RippleInner + Reach;
	Ripple->SetWorldLocation(RippleCenter + FVector(0.f, 0.f, 0.3f));
	Ripple->SetWorldScale3D(FVector(Half / 50.f, Half / 50.f, 1.f));
	UMaterialInstanceDynamic* MID = RippleMIDs[Slot];
	MID->SetVectorParameterValue(TEXT("Origin"), FLinearColor(RippleCenter.X, RippleCenter.Y, RippleCenter.Z));
	MID->SetScalarParameterValue(TEXT("ImpactTime"), ImpactTime);
	MID->SetScalarParameterValue(TEXT("Life"), RippleLife);
	MID->SetScalarParameterValue(TEXT("Size"), Weight);
	MID->SetScalarParameterValue(TEXT("Reach"), Reach);
	Ripple->SetVisibility(true);
	RippleEnd[Slot] = ImpactTime + RippleLife;
}

void AInkConductor::RetireExpired(double Now)
{
	auto Retire = [Now](auto& Comps, const TArray<double>& Ends)
	{
		for (int32 i = 0; i < Comps.Num(); ++i)
		{
			if (Ends[i] < Now && Comps[i]->IsVisible())
			{
				Comps[i]->SetVisibility(false);
			}
		}
	};
	Retire(Ripples, RippleEnd);
}

void AInkConductor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideSeal();
	Super::EndPlay(EndPlayReason);
}

void AInkConductor::ResetPainting()
{
	LastRippleTime = Idle;
	FinaleAt = -1.0;
	EndFinale();
	if (Calligraphy)
	{
		Calligraphy->Clear();
	}
	if (Landscape)
	{
		Landscape->Clear();
	}
}

void AInkConductor::FillLandscape()
{
	if (Landscape)
	{
		Landscape->Fill();
	}
}

void AInkConductor::LoadSong(const FString& IndexOrName)
{
	if (!IsValid(Piano) || !SongNamesProp)
	{
		return;
	}
	FScriptArrayHelper Songs(SongNamesProp, SongNamesProp->ContainerPtrToValuePtr<void>(Piano.Get()));
	FString Song;
	for (int32 i = 0; i < Songs.Num(); ++i)
	{
		const FString& Name = *reinterpret_cast<const FString*>(Songs.GetRawPtr(i));
		if ((IndexOrName.IsNumeric() && FCString::Atoi(*IndexOrName) == i) || (!IndexOrName.IsNumeric() && Name.Contains(IndexOrName)))
		{
			Song = Name;
			break;
		}
	}
	UFunction* LoadMidi = Piano->FindFunction(TEXT("Load MIDI"));
	if (Song.IsEmpty() || !LoadMidi)
	{
		UE_LOG(LogInk, Warning, TEXT("ink.Song: no song matches \"%s\""), *IndexOrName);
		return;
	}
	// the piano's own "Load MIDI" event takes the full path of the .mid file
	struct FLoadMidiParams
	{
		FString Name;
	};
	if (LoadMidi->ParmsSize != sizeof(FLoadMidiParams))
	{
		UE_LOG(LogInk, Warning, TEXT("ink.Song: the piano's Load MIDI event no longer takes a single path"));
		return;
	}
	FLoadMidiParams Params{ FPaths::ProjectContentDir() + TEXT("MIDI/") + Song };
	UE_LOG(LogInk, Display, TEXT("ink.Song: loading \"%s\""), *Song);
	Piano->ProcessEvent(LoadMidi, &Params);
}

void AInkConductor::StartFinale()
{
	APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (!Player || !Player->PlayerCameraManager || !FinaleCamera)
	{
		return;
	}
	FinaleAt = -1.0;
	FinaleStart = GetWorld()->GetTimeSeconds();
	FinaleFrom = Player->PlayerCameraManager->GetCameraLocation();
	FinaleFromRotation = Player->PlayerCameraManager->GetCameraRotation().Quaternion();
	FinaleFromFieldOfView = Player->PlayerCameraManager->GetFOVAngle();
	FinalePawnAt = Player->GetPawn() ? Player->GetPawn()->GetActorLocation() : FVector::ZeroVector;
	FinaleControl = Player->GetControlRotation();
	FinaleCamera->SetActorLocationAndRotation(FinaleFrom, FinaleFromRotation);
	FinaleCamera->GetCameraComponent()->SetFieldOfView(FinaleFromFieldOfView);
	Player->SetViewTargetWithBlend(FinaleCamera, 0.f);
	// whatever the song left unpainted rises while the view pulls back, so the painting is whole when it is sealed
	if (Landscape)
	{
		Landscape->Fill();
	}
	UE_LOG(LogInk, Display, TEXT("finale started at %.2f"), FinaleStart);
}

void AInkConductor::EndFinale()
{
	if (FinaleStart < 0.0)
	{
		return;
	}
	FinaleStart = -1.0;
	HideSeal();
	if (APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		Player->SetViewTargetWithBlend(Player->GetPawn(), 1.f);
	}
}

void AInkConductor::UpdateFinale(double Now)
{
	if (FinaleAt >= 0.0 && Now >= FinaleAt)
	{
		StartFinale();
	}
	if (FinaleStart < 0.0)
	{
		return;
	}
	// the player walking off or looking round takes the view back; a nudge of the mouse does not
	if (const APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		const APawn* Pawn = Player->GetPawn();
		if ((Pawn && FVector::Dist2D(Pawn->GetActorLocation(), FinalePawnAt) > 60.f) || !Player->GetControlRotation().Equals(FinaleControl, 10.f))
		{
			EndFinale();
			return;
		}
	}

	// a slow pull back and up, easing in and out, until the piano sits small in front of the whole scroll
	const float K = FMath::Clamp(float((Now - FinaleStart) / FinaleSeconds), 0.f, 1.f);
	const float Ease = K * K * K * (K * (K * 6.f - 15.f) + 10.f);
	const FVector Location = FMath::Lerp(FinaleFrom, FinaleLocation, Ease);
	const FQuat LookAt = (FinaleLookAt - Location).Rotation().Quaternion();
	FinaleCamera->SetActorLocationAndRotation(Location, FQuat::Slerp(FinaleFromRotation, LookAt, Ease));
	FinaleCamera->GetCameraComponent()->SetFieldOfView(FMath::Lerp(FinaleFromFieldOfView, FinaleFieldOfView, Ease));

	if (K >= 0.72f && !Seal)
	{
		ShowSeal();
		SealTime = Now;
	}
	if (SealStamp)
	{
		// stamped down: it lands a little oversized and settles
		const float T = FMath::Clamp(float((Now - SealTime) / 0.3), 0.f, 1.f);
		const float Scale = FMath::Lerp(1.6f, 1.f, 1.f - FMath::Square(1.f - T));
		SealStamp->SetRenderTransform(TransformCast<FSlateRenderTransform>(Concatenate(FScale2D(Scale), FQuat2D(FMath::DegreesToRadians(-4.f)))));
		SealStamp->SetRenderOpacity(FMath::Clamp(T * 2.f, 0.f, 1.f));
	}
}

void AInkConductor::ReadSong()
{
	// every key of the song and its length, so the landscape can fit its painting to the song's range and length
	TArray<int32> Keys;
	TArray<int32> Row;
	double Milliseconds = 0.0;
	int32 MelodyNotes = 0;
	for (int32 i = 0, Rows = RowCount(); i < Rows; ++i)
	{
		if (ReadRow(KeysProp, i, Row) && Row.Num() > 0)
		{
			// element 0 is the delay before the row, the notes follow; the top one of each row is the melody
			Milliseconds += Row[0];
			Keys.Append(Row.GetData() + 1, Row.Num() - 1);
			MelodyNotes += Row.Num() > 1 ? 1 : 0;
		}
	}
	Landscape->SetSong(Keys, Milliseconds / 1000.0);
	Calligraphy->SetSong(MelodyNotes, Milliseconds / 1000.0);
}

FString AInkConductor::CurrentSongName() const
{
	return SongNameProp ? SongNameProp->GetPropertyValue_InContainer(Piano.Get()) : FString();
}

void AInkConductor::ShowSeal()
{
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport)
	{
		return;
	}
	// a square vermilion seal: characters carved out of the stone inside a frame, the paste uneven and the
	// edges worn, as the stone material paints them
	const FLinearColor Carved(1.f, 0.94f, 0.86f);
	// a short Chinese name is cut the way seals are read: in columns, top to bottom, right to left
	FString SealText = CurrentSongName();
	const bool bChinese = SealText.Len() >= 2 && SealText.Len() <= 4 && Algo::AllOf(SealText, [](TCHAR C) { return C >= 0x4E00 && C <= 0x9FFF; });
	if (bChinese)
	{
		const FString S = SealText;
		SealText = S.Len() == 4 ? FString::Printf(TEXT("%c%c\n%c%c"), S[2], S[0], S[3], S[1]) : FString::JoinBy(S, TEXT("\n"), [](TCHAR C) { return FString(1, &C); });
	}
	TSharedRef<SWidget> Stone = SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush")).BorderBackgroundColor(SealColor);
	if (SealMaterial)
	{
		SealPaste = UMaterialInstanceDynamic::Create(SealMaterial, this);
		SealPaste->SetVectorParameterValue(TEXT("Paste"), SealColor);
		SealPaste->SetVectorParameterValue(TEXT("Carved"), Carved);
		SealBrush.SetResourceObject(SealPaste);
		SealBrush.ImageSize = FVector2D(180.f, 180.f);
		Stone = SNew(SImage).Image(&SealBrush);
	}
	SAssignNew(SealStamp, SBox)
		.WidthOverride(180.f)
		.HeightOverride(180.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				Stone
			]
			+ SOverlay::Slot()
			.Padding(28.f)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(SealText))
				.Font(BrushFont(SealText.Contains(TEXT("\n")) ? 34.f : 38.f))
				.ColorAndOpacity(Carved)
				.Justification(ETextJustify::Center)
				.AutoWrapText(true)
			]
		];
	SealStamp->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	SealStamp->SetRenderOpacity(0.f);
	Seal = SNew(SBox)
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Bottom)
		.Padding(FMargin(0.f, 0.f, 140.f, 110.f))
		[
			SealStamp.ToSharedRef()
		];
	Viewport->AddViewportWidgetContent(Seal.ToSharedRef(), 100);
}

void AInkConductor::RestyleHud()
{
	// the piano's hints and song title, written in ink on the paper instead of white type with a drop shadow
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (Widget->GetWorld() != GetWorld() || !Widget->WidgetTree || StyledHud.Contains(Widget))
		{
			continue;
		}
		StyledHud.Add(Widget);
		Widget->WidgetTree->ForEachWidget([this](UWidget* Child)
		{
			if (UTextBlock* Text = Cast<UTextBlock>(Child))
			{
				// the key hints have to stay legible, so only the song title is written with the brush
				const float Size = Text->GetFont().Size;
				const bool bHints = Text->GetText().ToString().Len() > 40;
				Text->SetFont(bHints ? FCoreStyle::GetDefaultFontStyle("Regular", Size - 2.f) : BrushFont(Size + 4.f));
				Text->SetColorAndOpacity(FSlateColor(HudColor));
				Text->SetShadowColorAndOpacity(FLinearColor::Transparent);
			}
		});
	}
}

FSlateFontInfo AInkConductor::BrushFont(float Size) const
{
	return BrushComposite ? FSlateFontInfo(BrushComposite, Size) : FCoreStyle::GetDefaultFontStyle("Regular", Size);
}

void AInkConductor::HideSeal()
{
	if (Seal)
	{
		if (UWorld* World = GetWorld())
		{
			if (UGameViewportClient* Viewport = World->GetGameViewport())
			{
				Viewport->RemoveViewportWidgetContent(Seal.ToSharedRef());
			}
		}
	}
	Seal.Reset();
	SealStamp.Reset();
}

namespace
{
	template <typename FunctionType>
	void ForEachConductor(UWorld* World, FunctionType Function)
	{
		for (TActorIterator<AInkConductor> It(World); It; ++It)
		{
			Function(**It);
		}
	}

	FAutoConsoleCommandWithWorld InkFinaleCommand(
		TEXT("ink.Finale"),
		TEXT("Pull the view back over the ink painting and stamp the seal now"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { ForEachConductor(World, [](AInkConductor& Conductor) { Conductor.StartFinale(); }); }));

	FAutoConsoleCommandWithWorld InkResetCommand(
		TEXT("ink.Reset"),
		TEXT("Wipe the ink painting and return to the player's view"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { ForEachConductor(World, [](AInkConductor& Conductor) { Conductor.ResetPainting(); }); }));

	FAutoConsoleCommandWithWorld InkGrowCommand(
		TEXT("ink.Grow"),
		TEXT("Raise every peak of the landscape to its full height now"),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { ForEachConductor(World, [](AInkConductor& Conductor) { Conductor.FillLandscape(); }); }));

	FAutoConsoleCommandWithWorldAndArgs InkSongCommand(
		TEXT("ink.Song"),
		TEXT("Load a song on the piano by its index in the song list or a part of its name, e.g. ink.Song 13"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() > 0)
			{
				ForEachConductor(World, [&Args](AInkConductor& Conductor) { Conductor.LoadSong(FString::Join(Args, TEXT(" "))); });
			}
		}));
}
