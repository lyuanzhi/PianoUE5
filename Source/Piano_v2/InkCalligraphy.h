// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "InkCalligraphy.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;

/**
 * The melody writes an inscription over the river, the way a painter inscribes a landscape: gao shan liu shui,
 * high mountains and flowing water, the old story of a qin player and the one friend who heard the mountains
 * and the river in his music. Every note of the melody moves the brush on, stroke by stroke in stroke order,
 * pausing as the brush lifts between strokes. The characters are real kaishu outlines; the ink is laid on
 * heavy at the start of each stroke and runs dry towards its end.
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

	// How many melody notes the song has and how long it lasts, so the inscription is written at a pace that
	// finishes it a little before the end of a short song, and within about a minute and a half of a long one
	void SetSong(int32 MelodyNotes, float Seconds);

	// Wipe the inscription
	void Clear();

	UPROPERTY(EditAnywhere, Category = "Ink")
	TObjectPtr<UMaterialInterface> InkMaterial;

	// Top of the right-hand column; the inscription is two columns read top to bottom, right to left
	UPROPERTY(EditAnywhere, Category = "Ink")
	FVector Top = FVector(-500.f, -7000.f, 4300.f);

	// Height of one character (cm)
	UPROPERTY(EditAnywhere, Category = "Ink")
	float CharacterSize = 600.f;

	// Characters in one column
	UPROPERTY(EditAnywhere, Category = "Ink")
	int32 PerColumn = 2;

	// How fast the brush travels along a stroke, in em units per second (a character is 1024 units tall), so a
	// dot is flicked in and a long stroke drawn out
	UPROPERTY(EditAnywhere, Category = "Ink")
	float BrushSpeed = 700.f;

	// How long the brush lifts between two strokes (seconds)
	UPROPERTY(EditAnywhere, Category = "Ink")
	float LiftSeconds = 0.18f;

protected:
	virtual void BeginPlay() override;

private:
	struct FNote
	{
		double Time;
		float Weight;
	};

	void BuildMesh();

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Mesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MID;

	TArray<FNote> Pending;
	// how much of the inscription the notes have asked for, and how much the brush has written (strokes, the
	// fraction being how far along the current one)
	float Target = 0.f;
	float Written = 0.f;
	float StrokesPerNote = 0.25f;
	// the brush is lifted between strokes until then
	double LiftedUntil = 0.0;
};
