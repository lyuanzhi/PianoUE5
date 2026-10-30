// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InkConductor.generated.h"

class ACameraActor;
class AInkCalligraphy;
class AInkLandscape;
class SWidget;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * Turns the piano's playback into ink: every note flicks a drop of ink off its key, released early enough
 * to hit the water on the frame the note sounds; the melody is written in the air by AInkCalligraphy and the
 * whole song raises a landscape with AInkLandscape. When the song ends the view pulls back over the painting
 * and a vermilion seal with the song's name is stamped on it. The song is read from the piano blueprint by
 * reflection, so the blueprint itself stays untouched.
 */
UCLASS()
class PIANO_V2_API AInkConductor : public AActor
{
	GENERATED_BODY()

public:
	AInkConductor();

	virtual void Tick(float DeltaSeconds) override;

	// Pull the view back over the painting and stamp the seal (ink.Finale)
	void StartFinale();

	// Back to the player's view, seal lifted
	void EndFinale();

	// Wipe the stroke and sink the landscape (ink.Reset)
	void ResetPainting();

	// Load a song on the piano by its index in the piano's song list or a part of its name (ink.Song)
	void LoadSong(const FString& IndexOrName);

	// Left empty, the first actor with an "Index" and a "Keys Array" is used
	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<AActor> Piano;

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> DropMaterial;

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> CloudMaterial;

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> StainMaterial;

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UStaticMesh> DropMesh;

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UStaticMesh> CloudMesh;

	// Seconds from the key to the water; each drop is released this long before its note
	UPROPERTY(EditAnywhere, Category = "Ink")
	float FallTime = 0.47f;

	// How far in front of a key's front edge its drop lands, out on open water where the ink reads against the paper
	UPROPERTY(EditAnywhere, Category = "Ink")
	float FrontOffset = 65.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float StainLife = 3.5f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float CloudLife = 3.5f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float CloudHalfXY = 85.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	float CloudHeight = 80.f;

	// Ray-march steps through a cloud; the main cost of the whole effect
	UPROPERTY(EditAnywhere, Category = "Ink")
	float CloudSteps = 18.f;

	UPROPERTY(EditAnywhere, Category = "Ink")
	int32 DropPoolSize = 32;

	UPROPERTY(EditAnywhere, Category = "Ink")
	int32 StainPoolSize = 48;

	// Clouds are ray-marched and expensive, so only this many billow at once
	UPROPERTY(EditAnywhere, Category = "Ink")
	int32 CloudPoolSize = 4;

	// A row raises a cloud only if its strongest note is at least this heavy
	UPROPERTY(EditAnywhere, Category = "Ink")
	float CloudMinWeight = 0.45f;

	// Press the piano's play key (Tab) by itself a second after the level starts, for showcases and tests
	UPROPERTY(EditAnywhere, Category = "Ink")
	bool bAutoPlayOnStart = false;

	// Seconds of silence after the last note before the view pulls back
	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	float FinaleDelay = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	float FinaleSeconds = 9.f;

	// Where the camera ends up, and what it looks at: the piano small against the whole scroll
	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	FVector FinaleLocation = FVector(0.f, 1450.f, 900.f);

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	FVector FinaleLookAt = FVector(0.f, -1150.f, 120.f);

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	float FinaleFieldOfView = 70.f;

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	// vermilion seal paste; Slate shows linear colour brighter than it reads here
	FLinearColor SealColor = FLinearColor(0.42f, 0.025f, 0.018f);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	bool FindPiano();
	void CacheKeys();
	void CreatePools();
	void PrewarmShaders();
	bool ReadRow(const FArrayProperty* RowsProp, int32 Row, TArray<int32>& Out) const;
	int32 RowCount() const;
	void FollowPlayback(double Now);
	void SpawnRow(int32 Row, double ImpactTime);
	void SpawnDrop(int32 Key, double ImpactTime, float Weight);
	void SpawnStain(const FVector& Impact, double ImpactTime, float Weight);
	void SpawnCloud(const FVector& Impact, double ImpactTime, float Weight);
	void MoveDrops(double Now);
	void RetireExpired(double Now);
	void SendPlayKey(EInputEvent Event) const;
	void UpdateFinale(double Now);
	void ShowSeal();
	void HideSeal();
	FString CurrentSongName() const;

	UPROPERTY()
	TObjectPtr<AInkCalligraphy> Calligraphy;

	UPROPERTY()
	TObjectPtr<AInkLandscape> Landscape;

	UPROPERTY()
	TObjectPtr<ACameraActor> FinaleCamera;

	double FinaleAt = -1.0;
	double FinaleStart = -1.0;
	FVector FinaleFrom = FVector::ZeroVector;
	FQuat FinaleFromRotation = FQuat::Identity;
	float FinaleFromFieldOfView = 90.f;
	double SealTime = -1.0;
	TSharedPtr<SWidget> Seal;
	TSharedPtr<SWidget> SealStamp;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Drops;

	UPROPERTY()
	TArray<TObjectPtr<UDecalComponent>> Stains;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Clouds;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> StainMIDs;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> CloudMIDs;

	// A drop flies from its key's front edge to its impact point, released FallTime before impact
	TArray<FVector> DropFrom;
	TArray<FVector> DropTo;
	TArray<double> DropImpact;
	TArray<double> StainEnd;
	TArray<double> CloudEnd;
	int32 NextDrop = 0;
	int32 NextStain = 0;

	// Piano blueprint variables, found once by name
	const FIntProperty* IndexProp = nullptr;
	const FBoolProperty* PauseProp = nullptr;
	const FArrayProperty* KeysProp = nullptr;
	const FArrayProperty* VelocitiesProp = nullptr;
	const FStrProperty* SongNameProp = nullptr;
	const FArrayProperty* SongNamesProp = nullptr;

	// Key index (0 = A0) -> top of its front edge, and where its drop lands on the water
	TArray<FVector> KeyEdge;
	TArray<FVector> KeyImpact;

	int32 LastIndex = INDEX_NONE;
	int32 LastRowCount = 0;
	int32 LastPlayedRow = -1;
	double RowPlayedTime = 0.0;
	// How much longer than its written delay each row really takes (the blueprint's Delay only fires on frame boundaries)
	double RowLag = 0.0;
	int32 ScheduledUpTo = -1;
	bool bFollowing = false;
};
