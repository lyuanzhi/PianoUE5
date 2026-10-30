// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "InkCalligraphy.generated.h"

class UMaterialInterface;

/**
 * The melody written in the air: a brush sweeps back and forth on an arc behind the piano, rising and
 * falling with the top note and pressing down on every note. The trail is a camera-facing ribbon that dries
 * out into fei bai as it ages.
 */
UCLASS()
class PIANO_V2_API AInkCalligraphy : public AActor
{
	GENERATED_BODY()

public:
	AInkCalligraphy();

	virtual void Tick(float DeltaSeconds) override;

	// A melody note: when it sounds, its key (0 = A0 .. 87 = C8) and how heavily it is played (0..1)
	void AddNote(double Time, int32 Key, float Weight);

	// Lift the brush and wipe the stroke
	void Clear();

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> StrokeMaterial;

	// The brush swings on a half circle of this radius around this point, behind the piano
	UPROPERTY(EditAnywhere, Category = "Ink")
	FVector ArcCenter = FVector(0.f, -200.f, 0.f);

	UPROPERTY(EditAnywhere, Category = "Ink")
	float ArcRadius = 480.f;

	// Seconds for one swing there and back
	UPROPERTY(EditAnywhere, Category = "Ink")
	float SwingSeconds = 14.f;

	// Heights of the lowest and the highest key
	UPROPERTY(EditAnywhere, Category = "Ink")
	float LowZ = 110.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float HighZ = 340.f;

	// How long the stroke stays on the air before it has dried away
	UPROPERTY(EditAnywhere, Category = "Ink")
	float TrailSeconds = 5.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float MinWidth = 8.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float MaxWidth = 70.f;

	// With no note for this long the brush lifts off
	UPROPERTY(EditAnywhere, Category = "Ink")
	float LiftAfter = 2.5f;

private:
	struct FNote
	{
		double Time;
		float Z;
		float Weight;
	};

	struct FTrailPoint
	{
		FVector Position;
		float Pressure;
		double Time;
		float Distance;
	};

	void UpdateBrush(float DeltaSeconds, double Now);
	void UpdateMesh(double Now);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;

	TArray<FNote> Pending;
	// oldest first
	TArray<FTrailPoint> Trail;

	double LastNoteTime = -1000.0;
	double LastSampleTime = -1000.0;
	double SwingTime = 0.0;
	float HeadZ = 0.f;
	float TargetZ = 0.f;
	float Pressure = 0.f;
	float Travelled = 0.f;
	bool bMeshCreated = false;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
};
