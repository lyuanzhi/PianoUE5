// Fill out your copyright notice in the Description page of Project Settings.


#include "InkLandscape.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// samples along the scroll and across each range
	constexpr int32 NumX = 180;
	constexpr int32 NumDepth = 12;

	float Hash(float X, float Seed)
	{
		return FMath::Frac(FMath::Sin(X * 12.9898f + Seed * 78.233f) * 43758.5453f);
	}

	// smooth 1D value noise, so ridges wander instead of jittering from vertex to vertex
	float Smooth(float X, float Seed)
	{
		const float Cell = FMath::FloorToFloat(X);
		return FMath::Lerp(Hash(Cell, Seed), Hash(Cell + 1.f, Seed), FMath::SmoothStep(0.f, 1.f, X - Cell));
	}
}

AInkLandscape::AInkLandscape()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	FarMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FarRange"));
	NearMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("NearRange"));
	for (UProceduralMeshComponent* Mesh : { FarMesh.Get(), NearMesh.Get() })
	{
		Mesh->SetupAttachment(RootComponent);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCastShadow(false);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mountain(TEXT("/Game/InkWash/Materials/M_InkMountain"));
	MountainMaterial = Mountain.Object;
	Random.Initialize(7);
}

void AInkLandscape::AddNote(double Time, float Progress, int32 Key, float Weight)
{
	Pending.Add({ Time, Progress, Key, Weight });
}

void AInkLandscape::Clear()
{
	Pending.Reset();
	for (FRange* Range : { &Far, &Near })
	{
		for (float& Height : Range->Target)
		{
			Height = 0.f;
		}
	}
}

void AInkLandscape::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (Far.Target.IsEmpty())
	{
		Far.Y = FarY;
		Far.Depth = FarDepth;
		Far.MaxHeight = FarMaxHeight;
		Far.Seed = 3.1f;
		Near.Y = NearY;
		Near.Depth = NearDepth;
		Near.MaxHeight = NearMaxHeight;
		Near.Seed = 11.7f;
		for (FRange* Range : { &Far, &Near })
		{
			Range->Target.Init(0.f, NumX);
			Range->Shown.Init(0.f, NumX);
		}
	}

	// notes arrive in time order; raise the land for every one that has sounded by now
	const double Now = GetWorld()->GetTimeSeconds();
	while (Pending.Num() > 0 && Pending[0].Time <= Now)
	{
		const FNote Note = Pending[0];
		Pending.RemoveAt(0);
		const bool bLow = Note.Key < SplitKey;
		FRange& Range = bLow ? Far : Near;
		// deep notes stand tallest in the far range, high notes tallest among the near hills
		const float Register = bLow
			? 1.f - 0.5f * Note.Key / float(SplitKey)
			: 0.55f + 0.45f * (Note.Key - SplitKey) / float(87 - SplitKey);
		const float Height = Range.MaxHeight * Register * (0.3f + 0.7f * Note.Weight);
		const float X = FMath::Lerp(-HalfWidth * 0.92f, HalfWidth * 0.92f, Note.Progress) + Random.FRandRange(-90.f, 90.f);
		Raise(Range, X, Height, Random.FRandRange(110.f, 260.f));
	}

	UpdateRange(Far, FarMesh, DeltaSeconds);
	UpdateRange(Near, NearMesh, DeltaSeconds);
}

void AInkLandscape::Raise(FRange& Range, float X, float Height, float Width)
{
	for (int32 i = 0; i < NumX; ++i)
	{
		const float SampleX = FMath::Lerp(-HalfWidth, HalfWidth, i / float(NumX - 1));
		const float Bump = Height * FMath::Exp(-FMath::Square((SampleX - X) / Width));
		// a new peak keeps the higher of the two, and notes played over and over slowly build the ridge up
		Range.Target[i] = FMath::Min(FMath::Max(Range.Target[i], Bump) + 0.06f * Bump, Range.MaxHeight * 1.25f);
	}
}

void AInkLandscape::UpdateRange(FRange& Range, UProceduralMeshComponent* Mesh, float DeltaSeconds)
{
	const float Blend = 1.f - FMath::Exp(-RiseRate * DeltaSeconds);
	bool bMoved = !Range.bCreated;
	for (int32 i = 0; i < NumX; ++i)
	{
		const float Gap = Range.Target[i] - Range.Shown[i];
		if (FMath::Abs(Gap) > 0.05f)
		{
			Range.Shown[i] += Gap * Blend;
			bMoved = true;
		}
	}
	if (!bMoved)
	{
		return;
	}

	const int32 Count = NumX * NumDepth;
	if (Triangles.IsEmpty())
	{
		for (int32 i = 0; i + 1 < NumX; ++i)
		{
			for (int32 k = 0; k + 1 < NumDepth; ++k)
			{
				// wound so the front faces look up (the material is two-sided, and a back face gets its normal flipped)
				const int32 V = i * NumDepth + k;
				Triangles.Append({ V, V + 1, V + NumDepth, V + 1, V + NumDepth + 1, V + NumDepth });
			}
		}
		Colors.Init(FLinearColor::White, Count);
		Tangents.Init(FProcMeshTangent(1.f, 0.f, 0.f), Count);
	}
	Vertices.SetNum(Count);
	Normals.SetNum(Count);
	UVs.SetNum(Count);

	const FTransform& ToWorld = Mesh->GetComponentTransform();
	for (int32 i = 0; i < NumX; ++i)
	{
		const float X = FMath::Lerp(-HalfWidth, HalfWidth, i / float(NumX - 1));
		// the ridge line wanders across the range so the peaks do not stand in a row
		const float RidgeAt = 0.5f + 0.18f * (Smooth(i / 9.f, Range.Seed) - 0.5f);
		for (int32 k = 0; k < NumDepth; ++k)
		{
			const float Across = k / float(NumDepth - 1);
			const float Ridge = FMath::Pow(FMath::Clamp(1.f - FMath::Abs(Across - RidgeAt) / FMath::Max(RidgeAt, 1.f - RidgeAt), 0.f, 1.f), 0.85f);
			const float Rough = 0.7f + 0.3f * Smooth(i / 4.f + k * 1.7f, Range.Seed + 5.f);
			// flat ground lies well under the water: a few centimetres would flicker through it at this distance
			constexpr float Sunk = 30.f;
			const float Z = WaterZ - Sunk + Ridge * (Range.Shown[i] * Rough + Sunk * FMath::Clamp(Range.Shown[i] / 20.f, 0.f, 1.f));
			const float Y = Range.Y + (Across - 0.5f) * Range.Depth;
			const int32 V = i * NumDepth + k;
			Vertices[V] = ToWorld.InverseTransformPosition(FVector(X, Y, Z));
			UVs[V] = FVector2D(X / 1000.f, Across);
		}
	}
	for (int32 i = 0; i < NumX; ++i)
	{
		for (int32 k = 0; k < NumDepth; ++k)
		{
			const FVector AlongX = Vertices[FMath::Min(i + 1, NumX - 1) * NumDepth + k] - Vertices[FMath::Max(i - 1, 0) * NumDepth + k];
			const FVector AlongY = Vertices[i * NumDepth + FMath::Min(k + 1, NumDepth - 1)] - Vertices[i * NumDepth + FMath::Max(k - 1, 0)];
			Normals[i * NumDepth + k] = FVector::CrossProduct(AlongX, AlongY).GetSafeNormal();
		}
	}

	if (!Range.bCreated)
	{
		Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false, false);
		Mesh->SetMaterial(0, MountainMaterial);
		Range.bCreated = true;
	}
	else
	{
		Mesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, UVs, Colors, Tangents, false);
	}
}
