// Fill out your copyright notice in the Description page of Project Settings.


#include "InkConductor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DecalComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
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

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Drop(TEXT("/Game/InkWash/Materials/M_InkDropSolid"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cloud(TEXT("/Game/InkWash/Materials/M_InkCloudEvt"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stain(TEXT("/Game/InkWash/Materials/M_InkStainEvt"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
	DropMaterial = Drop.Object;
	CloudMaterial = Cloud.Object;
	StainMaterial = Stain.Object;
	DropMesh = Sphere.Object;
	CloudMesh = Cube.Object;
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
	PrewarmShaders();

	FActorSpawnParameters Spawn;
	Spawn.Owner = this;
	Calligraphy = GetWorld()->SpawnActor<AInkCalligraphy>(AInkCalligraphy::StaticClass(), FTransform::Identity, Spawn);
	Landscape = GetWorld()->SpawnActor<AInkLandscape>(AInkLandscape::StaticClass(), FTransform::Identity, Spawn);
	Landscape->WaterZ = KeyImpact[NumKeys / 2].Z;
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
	KeyEdge.Init(FVector::ZeroVector, NumKeys);
	KeyImpact.Init(FVector::ZeroVector, NumKeys);
	const FVector Forward = Piano->GetActorForwardVector();
	FCollisionQueryParams TraceParams;
	TraceParams.AddIgnoredActor(Piano);

	TInlineComponentArray<UStaticMeshComponent*> Meshes(Piano);
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		// the keys are SM_Key1 (A0) .. SM_Key88 (C8)
		FString Number = Mesh->GetName();
		if (!Number.RemoveFromStart(TEXT("SM_Key")) || !Number.IsNumeric())
		{
			continue;
		}
		const int32 Key = FCString::Atoi(*Number) - 1;
		if (!KeyEdge.IsValidIndex(Key))
		{
			continue;
		}
		const FBoxSphereBounds& Bounds = Mesh->Bounds;
		const float HalfDepth = FMath::Abs(FVector::DotProduct(Bounds.BoxExtent, Forward));
		KeyEdge[Key] = Bounds.Origin + Forward * HalfDepth + FVector(0.f, 0.f, Bounds.BoxExtent.Z);

		FVector Impact = KeyEdge[Key] + Forward * FrontOffset;
		FHitResult Hit;
		Impact.Z = GetWorld()->LineTraceSingleByChannel(Hit, Impact, Impact - FVector(0.f, 0.f, 1000.f), ECC_Visibility, TraceParams) ? Hit.ImpactPoint.Z : 0.f;
		KeyImpact[Key] = Impact;
	}
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
		// ink is marked in custom depth so the ink-wash post process keeps it in ink tones
		Comp->SetRenderCustomDepth(true);
		Comp->SetVisibility(false);
		Comp->SetupAttachment(RootComponent);
		Comp->RegisterComponent();
		Comp->SetMaterial(0, Material);
		return Comp;
	};

	for (int32 i = 0; i < DropPoolSize; ++i)
	{
		Drops.Add(CreateMesh(DropMesh, DropMaterial));
	}
	DropFrom.Init(FVector::ZeroVector, Drops.Num());
	DropTo.Init(FVector::ZeroVector, Drops.Num());
	DropImpact.Init(Idle, Drops.Num());

	for (int32 i = 0; i < CloudPoolSize; ++i)
	{
		UStaticMeshComponent* Cloud = CreateMesh(CloudMesh, CloudMaterial);
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(CloudMaterial, Cloud);
		MID->SetScalarParameterValue(TEXT("Steps"), CloudSteps);
		Cloud->SetMaterial(0, MID);
		Clouds.Add(Cloud);
		CloudMIDs.Add(MID);
	}
	CloudEnd.Init(Idle, Clouds.Num());

	for (int32 i = 0; i < StainPoolSize; ++i)
	{
		UDecalComponent* Stain = NewObject<UDecalComponent>(this);
		// just big enough for the widest bleed and the ripples, which fade out at RingMax
		Stain->DecalSize = FVector(32.f, 110.f, 110.f);
		Stain->SetVisibility(false);
		Stain->SetupAttachment(RootComponent);
		Stain->RegisterComponent();
		UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(StainMaterial, Stain);
		MID->SetScalarParameterValue(TEXT("RingMax"), 100.f);
		Stain->SetDecalMaterial(MID);
		Stains.Add(Stain);
		StainMIDs.Add(MID);
	}
	StainEnd.Init(Idle, Stains.Num());
}

void AInkConductor::PrewarmShaders()
{
	// Show one cloud and one stain with a long-past impact time for the first half second: they draw nothing,
	// but their shaders get compiled before the first note instead of stalling it.
	if (!Clouds.IsEmpty())
	{
		Clouds[0]->SetWorldLocation(KeyImpact[NumKeys / 2]);
		Clouds[0]->SetVisibility(true);
		CloudEnd[0] = 0.5;
	}
	if (!Stains.IsEmpty())
	{
		Stains[0]->SetWorldLocationAndRotation(KeyImpact[NumKeys / 2], FRotator(-90.f, 0.f, 0.f));
		Stains[0]->SetVisibility(true);
		StainEnd[0] = 0.5;
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
	MoveDrops(Now);
	RetireExpired(Now);
	UpdateFinale(Now);
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
	// Release the drops of every row that sounds within the next FallTime seconds.
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
		if (RowTime - Now > FallTime)
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

	float Strongest = 0.f;
	FVector StrongestAt = FVector::ZeroVector;
	int32 TopKey = INDEX_NONE;
	float TopWeight = 0.f;
	const float Progress = RowIndex / float(FMath::Max(RowCount() - 1, 1));
	// element 0 of a row is its delay; the notes follow
	for (int32 i = 1; i < Keys.Num(); ++i)
	{
		const int32 Key = Keys[i];
		if (!KeyImpact.IsValidIndex(Key))
		{
			continue;
		}
		const float Velocity = Velocities.IsValidIndex(i) ? Velocities[i] : 64.f;
		// louder notes carry more ink, and low notes more than high ones
		const float Weight = FMath::Min(FMath::Clamp((Velocity - 20.f) / 90.f, 0.15f, 1.f) * FMath::Lerp(1.05f, 0.7f, Key / float(NumKeys - 1)), 1.f);
		SpawnDrop(Key, ImpactTime, Weight);
		SpawnStain(KeyImpact[Key], ImpactTime, Weight);
		Landscape->AddNote(ImpactTime, Progress, Key, Weight);
		if (Weight > Strongest)
		{
			Strongest = Weight;
			StrongestAt = KeyImpact[Key];
		}
		if (Key > TopKey)
		{
			TopKey = Key;
			TopWeight = Weight;
		}
	}
	if (Strongest >= CloudMinWeight)
	{
		SpawnCloud(StrongestAt, ImpactTime, Strongest);
	}
	if (TopKey != INDEX_NONE)
	{
		// the top note of each row is the melody the brush writes
		Calligraphy->AddNote(ImpactTime, TopKey, TopWeight);
	}
	UE_LOG(LogInk, Verbose, TEXT("row %d: %d notes, lands at %.3f"), RowIndex, Keys.Num() - 1, ImpactTime);
}

void AInkConductor::SpawnDrop(int32 Key, double ImpactTime, float Weight)
{
	if (Drops.IsEmpty())
	{
		return;
	}
	const int32 Slot = NextDrop++ % Drops.Num();
	const float Size = 0.05f + 0.035f * Weight;
	Drops[Slot]->SetWorldScale3D(FVector(Size, Size, Size * 1.6f));
	DropFrom[Slot] = KeyEdge[Key];
	DropTo[Slot] = KeyImpact[Key];
	DropImpact[Slot] = ImpactTime;
}

void AInkConductor::MoveDrops(double Now)
{
	for (int32 i = 0; i < Drops.Num(); ++i)
	{
		const double K = (Now - (DropImpact[i] - FallTime)) / FallTime;
		const bool bFalling = K >= 0.0 && K < 1.0;
		if (bFalling)
		{
			// flicked off the key: steady across, accelerating down
			FVector At = FMath::Lerp(DropFrom[i], DropTo[i], K);
			At.Z = DropFrom[i].Z - (DropFrom[i].Z - DropTo[i].Z) * K * K;
			Drops[i]->SetWorldLocation(At);
		}
		if (Drops[i]->IsVisible() != bFalling)
		{
			Drops[i]->SetVisibility(bFalling);
		}
	}
}

void AInkConductor::SpawnStain(const FVector& Impact, double ImpactTime, float Weight)
{
	if (Stains.IsEmpty())
	{
		return;
	}
	const int32 Slot = NextStain++ % Stains.Num();
	UDecalComponent* Stain = Stains[Slot];
	// decals project along their X axis: point it straight down onto the water
	Stain->SetWorldLocationAndRotation(Impact, FRotator(-90.f, 0.f, 0.f));
	UMaterialInstanceDynamic* MID = StainMIDs[Slot];
	MID->SetVectorParameterValue(TEXT("Origin"), FLinearColor(Impact.X, Impact.Y, Impact.Z));
	MID->SetScalarParameterValue(TEXT("ImpactTime"), ImpactTime);
	MID->SetScalarParameterValue(TEXT("Life"), StainLife);
	MID->SetScalarParameterValue(TEXT("Size"), Weight);
	Stain->SetVisibility(true);
	StainEnd[Slot] = ImpactTime + StainLife;
}

void AInkConductor::SpawnCloud(const FVector& Impact, double ImpactTime, float Weight)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const int32 Slot = CloudEnd.IndexOfByPredicate([Now](double End) { return End <= Now; });
	if (Slot == INDEX_NONE)
	{
		// every cloud is still billowing: skip this one rather than cut another short
		return;
	}
	UStaticMeshComponent* Cloud = Clouds[Slot];
	Cloud->SetWorldLocation(Impact + FVector(0.f, 0.f, CloudHeight * 0.5f));
	// the cube mesh is 100 units across
	Cloud->SetWorldScale3D(FVector(CloudHalfXY / 50.f, CloudHalfXY / 50.f, CloudHeight / 100.f));
	UMaterialInstanceDynamic* MID = CloudMIDs[Slot];
	MID->SetVectorParameterValue(TEXT("Origin"), FLinearColor(Impact.X, Impact.Y, Impact.Z));
	MID->SetScalarParameterValue(TEXT("ImpactTime"), ImpactTime);
	MID->SetScalarParameterValue(TEXT("Life"), CloudLife);
	MID->SetScalarParameterValue(TEXT("Size"), Weight);
	MID->SetScalarParameterValue(TEXT("HalfXY"), CloudHalfXY);
	MID->SetScalarParameterValue(TEXT("Height"), CloudHeight);
	Cloud->SetVisibility(true);
	CloudEnd[Slot] = ImpactTime + CloudLife;
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
	Retire(Stains, StainEnd);
	Retire(Clouds, CloudEnd);
}

void AInkConductor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	HideSeal();
	Super::EndPlay(EndPlayReason);
}

void AInkConductor::ResetPainting()
{
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
	FinaleCamera->SetActorLocationAndRotation(FinaleFrom, FinaleFromRotation);
	FinaleCamera->GetCameraComponent()->SetFieldOfView(FinaleFromFieldOfView);
	Player->SetViewTargetWithBlend(FinaleCamera, 0.f);
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
	// a square vermilion seal: carved white characters inside a thin white frame
	const FSlateBrush* Solid = FCoreStyle::Get().GetBrush("WhiteBrush");
	const FLinearColor Carved(1.f, 0.94f, 0.86f);
	SAssignNew(SealStamp, SBox)
		.WidthOverride(180.f)
		.HeightOverride(180.f)
		[
			SNew(SBorder)
			.BorderImage(Solid)
			.BorderBackgroundColor(SealColor)
			.Padding(9.f)
			[
				SNew(SBorder)
				.BorderImage(Solid)
				.BorderBackgroundColor(Carved)
				.Padding(3.f)
				[
					SNew(SBorder)
					.BorderImage(Solid)
					.BorderBackgroundColor(SealColor)
					.Padding(10.f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(CurrentSongName()))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 30))
						.ColorAndOpacity(Carved)
						.Justification(ETextJustify::Center)
						.AutoWrapText(true)
					]
				]
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
