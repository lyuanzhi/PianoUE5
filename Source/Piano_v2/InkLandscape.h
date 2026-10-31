// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "InkLandscape.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;

/**
 * A river landscape the music paints around the piano: karst peaks rise out of the mist all round as the song
 * goes on, high notes raising the near cliffs and low notes the distant ranges, sweeping from one side to the
 * other as the song progresses. The near cliffs, the boulders and trees at their feet are solid, so the player
 * can walk up to them and among them. The river itself is blank paper, and everything standing in it, the piano too, is mirrored
 * in the water.
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

	// Every key of the song about to play and how long it lasts: its own range decides which notes count as
	// high or low, and how much each note raises, so that every song paints the whole landscape by its end
	void SetSong(const TArray<int32>& Keys, float Seconds);

	// Sink the peaks back into the mist
	void Clear();

	// Raise every peak to its full height (ink.Grow)
	void Fill();

	// Mirror every mesh of this actor in the water, following it as it moves (the piano's keys)
	void Reflect(AActor* Source);

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> PeakMaterial;

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> ReflectionMaterial;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float WaterZ = 10.3f;

	// How much of its full height one fully weighted note raises a peak, until the song is known
	UPROPERTY(EditAnywhere, Category = "Ink")
	float GrowthPerNote = 0.16f;

	// How quickly peaks rise towards their height (per second)
	UPROPERTY(EditAnywhere, Category = "Ink")
	float RiseRate = 0.9f;

	// Picks the shapes of the peaks
	UPROPERTY(EditAnywhere, Category = "Ink")
	int32 Seed = 7;

protected:
	virtual void BeginPlay() override;

private:
	static constexpr int32 NumLayers = 4;

	struct FNote
	{
		double Time;
		float Progress;
		int32 Key;
		float Weight;
	};

	struct FPeak
	{
		FVector Foot;
		float Height = 0.f;
		// how much of it stands before any music
		float Rest = 0.f;
		float Target = 0.f;
		float Shown = -1.f;
		// its mesh in Meshes; the reflection is the one after it
		int32 Mesh = INDEX_NONE;
	};

	struct FShape
	{
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FLinearColor> Colors;
	};

	void BuildLayout();
	enum class EMound : uint8
	{
		Tower,
		Rock,
		Tree,
		Leaf,
	};

	// an islet is only a bank with trees, no tower
	void AddPeak(int32 Layer, float X, float Y, float Radius, float Height, bool bTrees, bool bIslet);
	void AddMound(FShape& Shape, const FVector& Offset, float Radius, float Height, float Tone, float Mist, EMound Kind, int32 Rings, int32 Segments);
	void AddTree(FShape& Shape, const FVector& Base, float Height);
	void ShowPeak(FPeak& Peak);
	int32 LayerOf(int32 Key) const;

	UPROPERTY()
	TArray<TObjectPtr<UProceduralMeshComponent>> Meshes;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Mirrored;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> MirrorSources;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> PeakMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ReflectionMID;

	TArray<FPeak> Peaks;
	// each layer's peaks, left to right
	TArray<int32> Layers[NumLayers];
	TArray<FNote> Pending;
	FRandomStream Random;
	// the song's low and high keys, and how much a note raises a peak in each layer
	int32 KeyLow = 20;
	int32 KeyHigh = 70;
	float LayerGrowth[NumLayers] = {};
	// a long song paints across the scroll several times, each pass raising the peaks further
	int32 Sweeps = 1;
};
