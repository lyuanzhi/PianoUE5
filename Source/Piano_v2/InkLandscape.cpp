// Fill out your copyright notice in the Description page of Project Settings.


#include "InkLandscape.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/CollisionProfile.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// per layer (near cliffs, middle ranges, far ranges, haze on the horizon): ink density, how high the mist
	// at the foot reaches (cm), and how much of a peak stands before any music
	constexpr float LayerTone[] = { 0.62f, 0.42f, 0.26f, 0.13f };
	constexpr float LayerMist[] = { 40.f, 200.f, 500.f, 900.f };
	constexpr float LayerRest[] = { 0.f, 0.f, 0.f, 0.6f };
	constexpr float TreeTone = 0.9f;

	// a world transform mirrored in the water plane at height Z
	FTransform MirrorInWater(const FTransform& Transform, float Z)
	{
		const FQuat Rotation = Transform.GetRotation();
		FVector Location = Transform.GetLocation();
		Location.Z = 2.f * Z - Location.Z;
		FVector Scale = Transform.GetScale3D();
		Scale.Z = -Scale.Z;
		// reflecting a rotation in a horizontal plane flips the x and y of its axis
		return FTransform(FQuat(-Rotation.X, -Rotation.Y, Rotation.Z, Rotation.W), Location, Scale);
	}
}

AInkLandscape::AInkLandscape()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Peak(TEXT("/Game/InkWash/Materials/M_InkPeak"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Reflection(TEXT("/Game/InkWash/Materials/M_InkReflection"));
	PeakMaterial = Peak.Object;
	ReflectionMaterial = Reflection.Object;
}

void AInkLandscape::BeginPlay()
{
	Super::BeginPlay();
	PeakMID = UMaterialInstanceDynamic::Create(PeakMaterial, this);
	PeakMID->SetScalarParameterValue(TEXT("WaterZ"), WaterZ);
	ReflectionMID = UMaterialInstanceDynamic::Create(ReflectionMaterial, this);
	ReflectionMID->SetScalarParameterValue(TEXT("WaterZ"), WaterZ);
	BuildLayout();
}

void AInkLandscape::BuildLayout()
{
	Random.Initialize(Seed);

	// Near cliffs all round the river, some close enough to walk up to: lower ones with trees on the left bank,
	// the tallest group on the right, more beside and behind the player, and the river open ahead.
	// { x, y, radius, height, trees }
	const float Near[][5] = {
		{ -5200.f, -4300.f, 480.f, 1200.f, 0.f }, { -3700.f, -3500.f, 520.f, 1500.f, 0.f }, { -2400.f, -2600.f, 420.f, 1150.f, 1.f },
		{ 2100.f, -3100.f, 480.f, 2300.f, 1.f }, { 3200.f, -3700.f, 560.f, 2900.f, 0.f }, { 4400.f, -4500.f, 520.f, 2100.f, 0.f },
		{ -3100.f, 300.f, 520.f, 1800.f, 1.f }, { 2900.f, 900.f, 600.f, 2200.f, 1.f },
		{ -2300.f, 3700.f, 480.f, 1700.f, 0.f }, { 1500.f, 4300.f, 560.f, 2000.f, 1.f }, { 4300.f, 3100.f, 450.f, 1600.f, 0.f }, { -4700.f, 2300.f, 430.f, 1300.f, 0.f } };
	for (const auto& P : Near)
	{
		AddPeak(0, P[0], P[1], P[2], P[3], P[4] > 0.f, false);
	}
	// islets with a few trees, out on the water near the piano
	AddPeak(0, -1500.f, -1700.f, 260.f, 0.f, true, true);
	AddPeak(0, 1700.f, 2000.f, 300.f, 0.f, true, true);

	const float Middle[][4] = {
		{ -7800.f, -7600.f, 700.f, 1800.f }, { -5600.f, -6800.f, 650.f, 2000.f }, { -3500.f, -7300.f, 600.f, 1700.f },
		{ -1400.f, -7800.f, 650.f, 2200.f }, { 1300.f, -7200.f, 750.f, 2700.f }, { 3300.f, -6400.f, 650.f, 2400.f },
		{ 5600.f, -7500.f, 800.f, 3100.f }, { 7800.f, -6900.f, 700.f, 2300.f },
		{ -7600.f, 1200.f, 750.f, 2600.f }, { 7200.f, 2400.f, 800.f, 2800.f }, { -5200.f, 6600.f, 700.f, 2400.f },
		{ 300.f, 8200.f, 800.f, 3000.f }, { 4700.f, 7100.f, 650.f, 2300.f } };
	for (const auto& P : Middle)
	{
		AddPeak(1, P[0], P[1], P[2], P[3], false, false);
	}
	// far ranges and haze all round the horizon, leaving gaps so they do not stand in a ring
	for (float Angle = 0.f; Angle < 360.f; Angle += 20.f)
	{
		if (Random.FRand() < 0.25f)
		{
			continue;
		}
		const float Direction = FMath::DegreesToRadians(Angle + Random.FRandRange(-6.f, 6.f));
		const float Distance = Random.FRandRange(11000.f, 14500.f);
		const float Height = Random.FRandRange(1600.f, 4500.f);
		AddPeak(2, Distance * FMath::Cos(Direction), Distance * FMath::Sin(Direction), Height * Random.FRandRange(0.25f, 0.4f), Height, false, false);
	}
	for (float Angle = 0.f; Angle < 360.f; Angle += 15.f)
	{
		if (Random.FRand() < 0.2f)
		{
			continue;
		}
		const float Direction = FMath::DegreesToRadians(Angle + Random.FRandRange(-5.f, 5.f));
		const float Distance = Random.FRandRange(19000.f, 24000.f);
		const float Height = Random.FRandRange(2200.f, 6500.f);
		AddPeak(3, Distance * FMath::Cos(Direction), Distance * FMath::Sin(Direction), Height * Random.FRandRange(0.25f, 0.4f), Height, false, false);
	}

	for (TArray<int32>& Layer : Layers)
	{
		Layer.Sort([this](int32 A, int32 B) { return Peaks[A].Foot.X < Peaks[B].Foot.X; });
	}
	for (FPeak& Peak : Peaks)
	{
		ShowPeak(Peak);
	}
	for (float& Growth : LayerGrowth)
	{
		Growth = GrowthPerNote;
	}
}

void AInkLandscape::AddPeak(int32 Layer, float X, float Y, float Radius, float Height, bool bTrees, bool bIslet)
{
	FShape Shape;
	// near and middle peaks are close enough to be seen in detail, so they get more of it
	const int32 Rings = Layer == 0 ? 44 : Layer == 1 ? 34 : 24;
	const int32 Segments = Layer == 0 ? 64 : Layer == 1 ? 48 : 36;
	if (!bIslet)
	{
		AddMound(Shape, FVector::ZeroVector, Radius, Height, LayerTone[Layer], LayerMist[Layer], EMound::Tower, Rings, Segments);
		// most peaks lean on a lower shoulder or two
		const int32 Shoulders = Layer < 3 && Random.FRand() < 0.7f ? Random.RandRange(1, 2) : 0;
		for (int32 i = 0; i < Shoulders; ++i)
		{
			const float Angle = Random.FRandRange(0.f, UE_TWO_PI);
			const FVector Offset = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * Radius * Random.FRandRange(0.5f, 0.9f);
			AddMound(Shape, Offset, Radius * Random.FRandRange(0.5f, 0.7f), Height * Random.FRandRange(0.45f, 0.75f), LayerTone[Layer], LayerMist[Layer], EMound::Tower, Rings * 3 / 4, Segments * 3 / 4);
		}
	}
	if (bTrees)
	{
		// at its foot, on the side facing the river and the piano, boulders at the water's edge with trees
		// among them (an islet is only that)
		const FVector Towards = bIslet ? FVector::ZeroVector : FVector(-X, -Y, 0.f).GetSafeNormal();
		const FVector Along(Towards.Y, -Towards.X, 0.f);
		const FVector Shore = Towards * Radius * 0.95f;
		const float Spread = Radius * (bIslet ? 0.9f : 1.1f);
		auto Scatter = [&]()
		{
			const FVector Loose = bIslet ? FVector(Random.FRandRange(-1.f, 1.f), Random.FRandRange(-1.f, 1.f), 0.f) : FVector::ZeroVector;
			return Shore + (Along * Random.FRandRange(-1.f, 1.f) + Towards * Random.FRandRange(-0.25f, 0.35f) + Loose) * Spread;
		};
		const int32 Rocks = Random.RandRange(4, 7);
		for (int32 i = 0; i < Rocks; ++i)
		{
			const float Size = Random.FRandRange(70.f, 220.f);
			AddMound(Shape, Scatter(), Size, Size * Random.FRandRange(0.5f, 1.1f), LayerTone[Layer], 6.f, EMound::Rock, 12, 20);
		}
		const int32 Trees = Random.RandRange(4, 7);
		for (int32 i = 0; i < Trees; ++i)
		{
			AddTree(Shape, Scatter(), Random.FRandRange(260.f, 520.f));
		}
	}

	FPeak& Peak = Peaks.AddDefaulted_GetRef();
	Peak.Foot = FVector(X, Y, WaterZ);
	Peak.Height = FMath::Max(Height, 150.f);
	// islets stand from the start and the music leaves them be
	Peak.Rest = bIslet ? 1.f : LayerRest[Layer];
	Peak.Target = Peak.Rest;
	Peak.Shown = Peak.Rest;
	Peak.Mesh = Meshes.Num();
	if (!bIslet)
	{
		Layers[Layer].Add(Peaks.Num() - 1);
	}

	// the peak, then its reflection: the same mesh hanging upside down from the same foot
	for (int32 Mirror = 0; Mirror < 2; ++Mirror)
	{
		if (Mirror)
		{
			for (FLinearColor& Color : Shape.Colors)
			{
				Color.A = 1.f;
			}
		}
		UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetCastShadow(false);
		// the peaks you can walk up to are solid; the distant ones and every reflection are only painted
		const bool bSolid = !Mirror && Layer < 2;
		Mesh->bUseAsyncCooking = true;
		Mesh->SetCollisionProfileName(bSolid ? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Mesh->RegisterComponent();
		Mesh->SetWorldLocation(Peak.Foot);
		// vertex colours carry data (tone, mist, kind, reflection), so no sRGB conversion
		Mesh->CreateMeshSection_LinearColor(0, Shape.Vertices, Shape.Triangles, Shape.Normals, Shape.UVs, Shape.Colors, TArray<FProcMeshTangent>(), bSolid, false);
		Mesh->SetMaterial(0, PeakMID);
		Meshes.Add(Mesh);
	}
}

void AInkLandscape::AddTree(FShape& Shape, const FVector& Base, float Height)
{
	// a thin dark trunk, and a crown of dabs of ink clustered round its top, the way a painter dots in leaves
	AddMound(Shape, Base, Height * 0.035f, Height * 0.9f, TreeTone, 4.f, EMound::Tree, 6, 8);
	const int32 Dabs = Random.RandRange(18, 32);
	for (int32 i = 0; i < Dabs; ++i)
	{
		const float Size = Height * Random.FRandRange(0.07f, 0.13f);
		const FVector At = Base + FVector(Random.FRandRange(-1.f, 1.f) * Height * 0.33f, Random.FRandRange(-1.f, 1.f) * Height * 0.33f, Height * Random.FRandRange(0.5f, 1.f) - Size * 0.5f);
		AddMound(Shape, At, Size, Size * Random.FRandRange(0.8f, 1.3f), TreeTone, 0.f, EMound::Leaf, 6, 9);
	}
}

void AInkLandscape::AddMound(FShape& Shape, const FVector& Offset, float Radius, float Height, float Tone, float Mist, EMound Kind, int32 Rings, int32 Segments)
{
	// A karst tower: near-vertical sides rounding off into a dome (or, for some, tapering to a point), bulging
	// and leaning, stepping in at ledges, fluted by the rain, and never smooth: layered noise pushes the rock in
	// and out. A rock is a rugged rounded boulder, a trunk a lumpy stem, a dab of leaves a closed lumpy ball.
	const bool bTower = Kind == EMound::Tower;
	const bool bLeaf = Kind == EMound::Leaf;
	const bool bTree = Kind == EMound::Tree || bLeaf;
	const bool bPointed = bTower && Random.FRand() < 0.35f;
	const float Sharpness = !bTower ? 2.f : bPointed ? Random.FRandRange(1.3f, 1.8f) : Random.FRandRange(2.5f, 5.f);
	const float Fullness = !bTower ? (bTree ? 0.5f : 0.6f) : bPointed ? Random.FRandRange(0.6f, 0.9f) : Random.FRandRange(0.35f, 0.6f);
	const float LedgeDepth = bTower ? Random.FRandRange(0.02f, 0.06f) : 0.f;
	const float LedgePhase = Random.FRandRange(0.f, UE_TWO_PI);
	const float Taper = bTower ? Random.FRandRange(0.15f, 0.4f) : 0.f;
	const int32 Lobes = Random.RandRange(2, 3);
	const float LobeDepth = Random.FRandRange(0.1f, 0.25f);
	const int32 Flutes = Random.RandRange(9, 15);
	// trees are lumpy all over, towers only grooved, banks neither
	const float FluteDepth = bTree ? Random.FRandRange(0.1f, 0.2f) : bTower ? Random.FRandRange(0.03f, 0.07f) : 0.f;
	const float Rugged = bTower ? Random.FRandRange(0.16f, 0.26f) : bTree ? 0.f : 0.3f;
	const FVector NoiseAt(Random.FRandRange(0.f, 100.f), Random.FRandRange(0.f, 100.f), Random.FRandRange(0.f, 100.f));
	const float Phase = Random.FRandRange(0.f, UE_TWO_PI);
	const float Twist = Random.FRandRange(-2.f, 2.f);
	const FVector2D Lean = bTower ? FVector2D(Random.FRandRange(-1.f, 1.f), Random.FRandRange(-1.f, 1.f)) * Random.FRandRange(0.f, 0.5f) * Radius : FVector2D::ZeroVector;
	const FLinearColor Color(Tone, Mist / 1000.f, bTree ? 1.f : 0.f, 0.f);

	const int32 First = Shape.Vertices.Num();
	for (int32 j = 0; j < Rings; ++j)
	{
		const float H = j / float(Rings - 1);
		const float Profile = bLeaf ? FMath::Sqrt(FMath::Max(1.f - FMath::Square(2.f * H - 1.f), 0.f)) : (1.f - Taper * H) * FMath::Pow(FMath::Max(1.f - FMath::Pow(H, Sharpness), 0.f), Fullness);
		for (int32 k = 0; k < Segments; ++k)
		{
			const float Angle = UE_TWO_PI * k / Segments;
			const float Lump = 1.f + LobeDepth * FMath::Sin(Lobes * Angle + Phase + Twist * H) + 0.5f * LobeDepth * FMath::Sin((2 * Lobes + 1) * Angle - Phase + 3.f * H)
				+ FluteDepth * FMath::Sin(Flutes * Angle + 2.f * FMath::Sin(5.f * H + Phase))
				+ LedgeDepth * FMath::Sin(UE_TWO_PI * 4.f * H + LedgePhase + Angle);
			// bulges, hollows and crevices, in the mound's own proportions so big and small rock look alike
			const FVector Unit(FMath::Cos(Angle) * Profile, FMath::Sin(Angle) * Profile, H * Height / Radius);
			const float Noise = Rugged * (FMath::PerlinNoise3D(Unit * 1.1f + NoiseAt) + 0.45f * FMath::PerlinNoise3D(Unit * 2.9f + NoiseAt) + 0.2f * FMath::PerlinNoise3D(Unit * 7.3f + NoiseAt));
			const float R = Radius * Profile * Lump * (1.f + Noise);
			Shape.Vertices.Add(Offset + FVector(R * FMath::Cos(Angle) + Lean.X * H * H, R * FMath::Sin(Angle) + Lean.Y * H * H, H * Height * (1.f + 0.3f * Noise)));
			Shape.UVs.Add(FVector2D(k / float(Segments), H));
			Shape.Colors.Add(Color);
		}
	}

	auto At = [&](int32 J, int32 K) { return Shape.Vertices[First + FMath::Clamp(J, 0, Rings - 1) * Segments + (K + Segments) % Segments]; };
	for (int32 j = 0; j < Rings; ++j)
	{
		for (int32 k = 0; k < Segments; ++k)
		{
			const FVector Normal = FVector::CrossProduct(At(j, k + 1) - At(j, k - 1), At(j + 1, k) - At(j - 1, k)).GetSafeNormal();
			// the summit ring collapses to a point
			Shape.Normals.Add(Normal.IsNearlyZero() ? FVector::UpVector : Normal);
		}
	}
	for (int32 j = 0; j + 1 < Rings; ++j)
	{
		for (int32 k = 0; k < Segments; ++k)
		{
			const int32 V00 = First + j * Segments + k;
			const int32 V01 = First + j * Segments + (k + 1) % Segments;
			// wound so the outside faces out
			Shape.Triangles.Append({ V00, V00 + Segments, V01, V00 + Segments, V01 + Segments, V01 });
		}
	}
}

void AInkLandscape::ShowPeak(FPeak& Peak)
{
	const bool bVisible = Peak.Shown > 0.005f;
	// a peak rises out of the mist on the river, keeping its shape and stretching up a little as it comes;
	// the material cuts it off at the water, and its reflection hangs below
	const float Sink = (1.f - Peak.Shown) * Peak.Height * 0.6f;
	const float Stretch = 0.4f + 0.6f * Peak.Shown;
	for (int32 Mirror = 0; Mirror < 2; ++Mirror)
	{
		UProceduralMeshComponent* Mesh = Meshes[Peak.Mesh + Mirror];
		Mesh->SetVisibility(bVisible);
		if (bVisible)
		{
			Mesh->SetWorldLocation(Peak.Foot + FVector(0.f, 0.f, Mirror ? Sink : -Sink));
			Mesh->SetWorldScale3D(FVector(1.f, 1.f, Mirror ? -Stretch : Stretch));
		}
	}
}

void AInkLandscape::AddNote(double Time, float Progress, int32 Key, float Weight)
{
	Pending.Add({ Time, Progress, Key, Weight });
}

void AInkLandscape::SetSong(const TArray<int32>& Keys, float Seconds)
{
	if (Keys.IsEmpty())
	{
		return;
	}
	// one pass across the scroll takes about a minute at most, so a long song does not leave it bare for minutes
	Sweeps = FMath::Max(1, FMath::RoundToInt(Seconds / 50.f));
	TArray<int32> Sorted = Keys;
	Sorted.Sort();
	KeyLow = Sorted[Sorted.Num() / 10];
	KeyHigh = Sorted[Sorted.Num() * 9 / 10];
	int32 Notes[NumLayers] = {};
	for (int32 Key : Keys)
	{
		++Notes[LayerOf(Key)];
	}
	for (int32 Layer = 0; Layer < NumLayers; ++Layer)
	{
		// enough for each peak to stand at full height about halfway through its share of the song (notes average
		// about half weight), so peaks come up promptly as the music reaches them
		LayerGrowth[Layer] = FMath::Clamp(2.5f * Layers[Layer].Num() / FMath::Max(Notes[Layer] * 0.55f, 1.f), 0.03f, 0.6f);
	}
}

int32 AInkLandscape::LayerOf(int32 Key) const
{
	// high notes raise the near cliffs, low notes the distant ranges
	const float Register = (Key - KeyLow) / float(FMath::Max(KeyHigh - KeyLow, 1));
	return Register >= 0.7f ? 0 : Register >= 0.4f ? 1 : Register >= 0.15f ? 2 : 3;
}

void AInkLandscape::Clear()
{
	Pending.Reset();
	for (FPeak& Peak : Peaks)
	{
		Peak.Target = Peak.Rest;
	}
}

void AInkLandscape::Fill()
{
	for (FPeak& Peak : Peaks)
	{
		Peak.Target = 1.f;
	}
}

void AInkLandscape::Reflect(AActor* Source)
{
	TArray<UStaticMeshComponent*> Parts;
	Source->GetComponents(Parts);
	for (UStaticMeshComponent* Part : Parts)
	{
		if (!Part->GetStaticMesh() || !Part->IsVisible())
		{
			continue;
		}
		UStaticMeshComponent* Copy = NewObject<UStaticMeshComponent>(this);
		Copy->SetMobility(EComponentMobility::Movable);
		Copy->SetupAttachment(RootComponent);
		Copy->SetStaticMesh(Part->GetStaticMesh());
		Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Copy->SetCastShadow(false);
		Copy->RegisterComponent();
		for (int32 Slot = 0; Slot < Copy->GetNumMaterials(); ++Slot)
		{
			Copy->SetMaterial(Slot, ReflectionMID);
		}
		Copy->SetWorldTransform(MirrorInWater(Part->GetComponentTransform(), WaterZ));
		Mirrored.Add(Copy);
		MirrorSources.Add(Part);
	}
}

void AInkLandscape::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// notes arrive in time order; every one that has sounded by now raises a peak
	const double Now = GetWorld()->GetTimeSeconds();
	while (Pending.Num() > 0 && Pending[0].Time <= Now)
	{
		const FNote Note = Pending[0];
		Pending.RemoveAt(0);
		const int32 Layer = LayerOf(Note.Key);
		const TArray<int32>& Row = Layers[Layer];
		if (Row.IsEmpty())
		{
			continue;
		}
		// and the song runs across the picture from left to right, once per sweep
		const float Across = FMath::Min(FMath::Frac(Note.Progress * Sweeps), 0.9999f);
		const int32 Slot = FMath::Clamp(FMath::RoundToInt(Across * (Row.Num() - 1)) + Random.RandRange(-1, 1), 0, Row.Num() - 1);
		FPeak& Peak = Peaks[Row[Slot]];
		Peak.Target = FMath::Min(Peak.Target + LayerGrowth[Layer] * Note.Weight, 1.f);
	}

	const float Blend = 1.f - FMath::Exp(-RiseRate * DeltaSeconds);
	for (FPeak& Peak : Peaks)
	{
		if (FMath::Abs(Peak.Target - Peak.Shown) > 0.001f)
		{
			Peak.Shown += (Peak.Target - Peak.Shown) * Blend;
			ShowPeak(Peak);
		}
	}

	// the piano's keys move, so its reflection follows them
	for (int32 i = 0; i < Mirrored.Num(); ++i)
	{
		if (IsValid(MirrorSources[i]))
		{
			const FTransform Transform = MirrorInWater(MirrorSources[i]->GetComponentTransform(), WaterZ);
			if (!Mirrored[i]->GetComponentTransform().Equals(Transform, 0.01))
			{
				Mirrored[i]->SetWorldTransform(Transform);
			}
		}
	}
}
