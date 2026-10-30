// Fill out your copyright notice in the Description page of Project Settings.


#include "InkCalligraphy.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// trail samples per second, and the most the ribbon can hold
	constexpr float SampleRate = 40.f;
	constexpr int32 MaxPoints = 240;
}

AInkCalligraphy::AInkCalligraphy()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Stroke"));
	RootComponent = Mesh;
	Mesh->SetCastShadow(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// the stroke is ink: custom depth keeps it in ink tones in the ink-wash post process
	Mesh->SetRenderCustomDepth(true);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Stroke(TEXT("/Game/InkWash/Materials/M_InkStroke"));
	StrokeMaterial = Stroke.Object;
}

void AInkCalligraphy::AddNote(double Time, int32 Key, float Weight)
{
	Pending.Add({ Time, FMath::Lerp(LowZ, HighZ, FMath::Clamp(Key / 87.f, 0.f, 1.f)), Weight });
}

void AInkCalligraphy::Clear()
{
	Pending.Reset();
	Trail.Reset();
	LastNoteTime = -1000.0;
	Pressure = 0.f;
	Mesh->SetVisibility(false);
}

void AInkCalligraphy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();
	UpdateBrush(DeltaSeconds, Now);
	UpdateMesh(Now);
}

void AInkCalligraphy::UpdateBrush(float DeltaSeconds, double Now)
{
	// notes arrive in time order; take every one that has sounded by now
	while (Pending.Num() > 0 && Pending[0].Time <= Now)
	{
		if (Now - LastNoteTime > LiftAfter)
		{
			// the brush comes down right where the phrase starts
			HeadZ = Pending[0].Z;
		}
		TargetZ = Pending[0].Z;
		Pressure = FMath::Max(Pressure, Pending[0].Weight);
		LastNoteTime = Pending[0].Time;
		Pending.RemoveAt(0);
	}

	Pressure *= FMath::Exp(-3.5f * DeltaSeconds);
	// glide towards the melody's height; exponential, so even a long hitch (loading a song) cannot overshoot
	HeadZ = FMath::Lerp(HeadZ, TargetZ, 1.f - FMath::Exp(-6.f * DeltaSeconds));

	if (Now - LastNoteTime > LiftAfter || Now - LastSampleTime < 1.0 / SampleRate)
	{
		return;
	}
	SwingTime += Now - FMath::Max(LastSampleTime, Now - 0.1);
	LastSampleTime = Now;

	const float Angle = UE_HALF_PI * 0.85f * FMath::Sin(UE_TWO_PI * SwingTime / SwingSeconds);
	const FVector Position = ArcCenter + FVector(ArcRadius * FMath::Sin(Angle), -ArcRadius * FMath::Cos(Angle), HeadZ + 10.f * FMath::Sin(SwingTime * 2.3));
	if (Position.ContainsNaN())
	{
		// never let a bad sample into the mesh: NaN bounds stop the whole stroke from rendering
		HeadZ = TargetZ;
		return;
	}
	if (Trail.Num() > 0)
	{
		Travelled += FVector::Dist(Trail.Last().Position, Position);
	}
	Trail.Add({ Position, Pressure, Now, Travelled });
	if (Trail.Num() > MaxPoints)
	{
		Trail.RemoveAt(0);
	}
}

void AInkCalligraphy::UpdateMesh(double Now)
{
	while (Trail.Num() > 0 && Now - Trail[0].Time > TrailSeconds)
	{
		Trail.RemoveAt(0);
	}
	if (Trail.Num() < 2)
	{
		Mesh->SetVisibility(false);
		return;
	}

	FVector Eye = FVector::ZeroVector;
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (Player && Player->PlayerCameraManager)
	{
		Eye = Player->PlayerCameraManager->GetCameraLocation();
	}

	if (!bMeshCreated)
	{
		Vertices.Init(FVector::ZeroVector, MaxPoints * 2);
		Normals.Init(FVector::UpVector, MaxPoints * 2);
		UVs.Init(FVector2D::ZeroVector, MaxPoints * 2);
		Colors.Init(FLinearColor::Transparent, MaxPoints * 2);
		Tangents.Init(FProcMeshTangent(1.f, 0.f, 0.f), MaxPoints * 2);
		for (int32 i = 0; i + 1 < MaxPoints; ++i)
		{
			const int32 A = 2 * i;
			Triangles.Append({ A, A + 1, A + 2, A + 1, A + 3, A + 2 });
		}
	}

	const FTransform& ToWorld = Mesh->GetComponentTransform();
	const int32 Count = Trail.Num();
	const float HeadDistance = Trail.Last().Distance;
	for (int32 i = 0; i < MaxPoints; ++i)
	{
		// slots beyond the trail collapse onto the newest point, invisible
		const int32 At = FMath::Min(i, Count - 1);
		const FTrailPoint& Point = Trail[At];
		const FVector Along = (Trail[FMath::Min(At + 1, Count - 1)].Position - Trail[FMath::Max(At - 1, 0)].Position).GetSafeNormal();
		// facing the camera, so from any side it reads as one stroke of a brush
		const FVector Side = FVector::CrossProduct(Along, (Eye - Point.Position).GetSafeNormal()).GetSafeNormal();
		const float Age = FMath::Clamp(float((Now - Point.Time) / TrailSeconds), 0.f, 1.f);
		// the last few centimetres taper to the brush tip, and old ink thins as it dries
		const float Tip = FMath::Clamp((HeadDistance - Point.Distance) / 15.f, 0.f, 1.f);
		const float Width = (MinWidth + (MaxWidth - MinWidth) * Point.Pressure) * (1.f - 0.45f * Age) * (0.25f + 0.75f * Tip);
		const float Fade = i < Count ? 1.f - FMath::SmoothStep(0.7f, 1.f, Age) : 0.f;
		for (int32 Edge = 0; Edge < 2; ++Edge)
		{
			const int32 V = 2 * i + Edge;
			Vertices[V] = ToWorld.InverseTransformPosition(Point.Position + Side * Width * (Edge == 0 ? -0.5f : 0.5f));
			UVs[V] = FVector2D(Point.Distance / 100.f, Edge);
			Colors[V] = FLinearColor(Point.Pressure, Age, Fade, 1.f);
		}
	}

	// vertex colours carry data (pressure, age, fade), so no sRGB conversion
	if (!bMeshCreated)
	{
		Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, Colors, Tangents, false, false);
		Mesh->SetMaterial(0, StrokeMaterial);
		bMeshCreated = true;
	}
	else
	{
		Mesh->UpdateMeshSection_LinearColor(0, Vertices, Normals, UVs, Colors, Tangents, false);
	}
	Mesh->SetVisibility(true);
}
