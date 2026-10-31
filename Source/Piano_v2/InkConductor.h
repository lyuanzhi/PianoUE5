// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "GameFramework/Actor.h"
#include "InkConductor.generated.h"

class ACameraActor;
class AInkCalligraphy;
class AInkLandscape;
class SWidget;
class UFontFace;
struct FCompositeFont;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UUserWidget;

/**
 * Turns the piano's playback into ink: the notes raise a landscape around the river with AInkLandscape, the
 * melody writes an inscription over it with AInkCalligraphy, and the piano's sound sends rings across the
 * water around it. When the song ends the view pulls back over the painting
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

	// Raise every peak to its full height (ink.Grow)
	void FillLandscape();

	// Load a song on the piano by its index in the piano's song list or a part of its name (ink.Song)
	void LoadSong(const FString& IndexOrName);

	// Left empty, the first actor with an "Index" and a "Keys Array" is used
	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<AActor> Piano;

	// Rings running across the water from around the piano as it plays
	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> RippleMaterial;

	// A flat square the rings are drawn on, lying on the water
	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UStaticMesh> RippleMesh;

	// How far ahead rows are read, so their notes reach the landscape, the inscription and the water on the
	// frame they sound
	UPROPERTY(EditAnywhere, Category = "Ink")
	float LookAhead = 0.47f;

	// Seconds a ring takes to run out and fade away
	UPROPERTY(EditAnywhere, Category = "Ink")
	float RippleLife = 4.f;

	// How far beyond the piano the rings of the lightest and of the heaviest note travel (cm)
	UPROPERTY(EditAnywhere, Category = "Ink")
	FVector2D RippleReach = FVector2D(350.f, 900.f);

	UPROPERTY(EditAnywhere, Category = "Ink")
	int32 RipplePoolSize = 16;

	// Rows closer together than this share one ring
	UPROPERTY(EditAnywhere, Category = "Ink")
	float MinRippleInterval = 0.35f;

	// Press the piano's play key (Tab) by itself a second after the level starts, for showcases and tests
	UPROPERTY(EditAnywhere, Category = "Ink")
	bool bAutoPlayOnStart = false;

	// Seconds of silence after the last note before the view pulls back
	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	float FinaleDelay = 2.5f;

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	float FinaleSeconds = 9.f;

	// Where the camera ends up, and what it looks at: the piano small in front of the whole river
	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	FVector FinaleLocation = FVector(-400.f, 3200.f, 700.f);

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	FVector FinaleLookAt = FVector(200.f, -6000.f, 1100.f);

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	float FinaleFieldOfView = 70.f;

	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	// vermilion seal paste; Slate shows linear colour brighter than it reads here
	FLinearColor SealColor = FLinearColor(0.42f, 0.025f, 0.018f);

	// The seal's stone: uneven paste, a worn border and frame (a UI material)
	UPROPERTY(EditAnywhere, Category = "Ink|Finale")
	TObjectPtr<UMaterialInterface> SealMaterial;

	// Brush lettering for the seal and the piano's on-screen hints
	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UFontFace> BrushFace;

	// The piano's hints and song title are written in this ink instead of white type
	UPROPERTY(EditAnywhere, Category = "Ink")
	FLinearColor HudColor = FLinearColor(0.02f, 0.02f, 0.024f, 0.75f);

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
	void SpawnRipple(double ImpactTime, float Weight);
	void RetireExpired(double Now);
	void SendPlayKey(EInputEvent Event) const;
	void UpdateFinale(double Now);
	void ShowSeal();
	void HideSeal();
	void RestyleHud();
	FSlateFontInfo BrushFont(float Size) const;
	FString CurrentSongName() const;
	void ReadSong();

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
	// where the player stood and looked when the finale began: moving away from it takes the view back
	FVector FinalePawnAt = FVector::ZeroVector;
	FRotator FinaleControl = FRotator::ZeroRotator;
	double SealTime = -1.0;
	double NextHudCheck = 0.0;
	TSet<TWeakObjectPtr<UUserWidget>> StyledHud;
	TSharedPtr<const FCompositeFont> BrushComposite;
	TSharedPtr<SWidget> Seal;
	TSharedPtr<SWidget> SealStamp;
	FSlateBrush SealBrush;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> SealPaste;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Ripples;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RippleMIDs;

	TArray<double> RippleEnd;
	int32 NextRipple = 0;
	double LastRippleTime = -1000.0;
	// where the rings start: the middle of the piano on the water, and how far out its feet stand
	FVector RippleCenter = FVector::ZeroVector;
	float RippleInner = 180.f;

	// Piano blueprint variables, found once by name
	const FIntProperty* IndexProp = nullptr;
	const FBoolProperty* PauseProp = nullptr;
	const FArrayProperty* KeysProp = nullptr;
	const FArrayProperty* VelocitiesProp = nullptr;
	const FStrProperty* SongNameProp = nullptr;
	const FArrayProperty* SongNamesProp = nullptr;

	// the height of the water, found under the piano
	float WaterZ = 10.f;

	int32 LastIndex = INDEX_NONE;
	int32 LastRowCount = 0;
	int32 LastPlayedRow = -1;
	double RowPlayedTime = 0.0;
	// How much longer than its written delay each row really takes (the blueprint's Delay only fires on frame boundaries)
	double RowLag = 0.0;
	int32 ScheduledUpTo = -1;
	bool bFollowing = false;
};
