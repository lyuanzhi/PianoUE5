// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "InkLandscape.generated.h"

class UMaterialInterface;

/**
 * A handscroll landscape the music paints behind the piano: as the song goes on, mountains rise out of the
 * water from left to right. Low notes pile up the tall far range, high notes the low hills in front, and the
 * harder a note is played the higher it lifts the land. Every song leaves a different landscape.
 */
UCLASS()
class PIANO_V2_API AInkLandscape : public AActor
{
	GENERATED_BODY()

public:
	AInkLandscape();

	virtual void Tick(float DeltaSeconds) override;

	// A note that sounds at Time, Progress (0..1) of the way through the song
	void AddNote(double Time, float Progress, int32 Key, float Weight);

	// Sink every mountain back into the water
	void Clear();

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> MountainMaterial;

	// The scroll runs along X from -HalfWidth to +HalfWidth
	UPROPERTY(EditAnywhere, Category = "Ink")
	float HalfWidth = 2600.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float WaterZ = 10.3f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float FarY = -1750.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float FarDepth = 700.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float FarMaxHeight = 560.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float NearY = -980.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float NearDepth = 420.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float NearMaxHeight = 230.f;

	// Keys below this one build the far range, the rest the near hills
	UPROPERTY(EditAnywhere, Category = "Ink")
	int32 SplitKey = 44;

	// How quickly mountains rise towards their height (per second)
	UPROPERTY(EditAnywhere, Category = "Ink")
	float RiseRate = 1.2f;

private:
	struct FNote
	{
		double Time;
		float Progress;
		int32 Key;
		float Weight;
	};

	struct FRange
	{
		float Y = 0.f;
		float Depth = 0.f;
		float MaxHeight = 0.f;
		float Seed = 0.f;
		TArray<float> Target;
		TArray<float> Shown;
		bool bCreated = false;
	};

	void Raise(FRange& Range, float X, float Height, float Width);
	void UpdateRange(FRange& Range, UProceduralMeshComponent* Mesh, float DeltaSeconds);

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> FarMesh;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> NearMesh;

	FRange Far;
	FRange Near;
	TArray<FNote> Pending;
	FRandomStream Random;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
};
