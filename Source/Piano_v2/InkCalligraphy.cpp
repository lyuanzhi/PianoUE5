// Fill out your copyright notice in the Description page of Project Settings.


#include "InkCalligraphy.h"
#include "Engine/World.h"
#include "InkGlyphs.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AInkCalligraphy::AInkCalligraphy()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Inscription"));
	RootComponent = Mesh;
	Mesh->SetCastShadow(false);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ink(TEXT("/Game/InkWash/Materials/M_InkScript"));
	InkMaterial = Ink.Object;
}

void AInkCalligraphy::BeginPlay()
{
	Super::BeginPlay();
	BuildMesh();
}

void AInkCalligraphy::BuildMesh()
{
	const int32 NumVertices = UE_ARRAY_COUNT(InkGlyphs::Vertices);
	const int32 NumGlyphs = UE_ARRAY_COUNT(InkGlyphs::Glyphs);
	const float Scale = CharacterSize / 1024.f;

	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> AlongAcross;
	TArray<FVector2D> Order;
	TArray<FVector2D> Unused;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	const TArray<int32> Triangles(InkGlyphs::Triangles, UE_ARRAY_COUNT(InkGlyphs::Triangles));

	int32 Glyph = 0;
	for (int32 i = 0; i < NumVertices; ++i)
	{
		while (Glyph + 1 < NumGlyphs && i >= InkGlyphs::Glyphs[Glyph + 1][0])
		{
			++Glyph;
		}
		const float* V = InkGlyphs::Vertices[i];
		// down each column, the columns from right to left; the em square has y up and its top at 900
		const int32 Column = Glyph / FMath::Max(PerColumn, 1);
		const int32 Row = Glyph % FMath::Max(PerColumn, 1);
		Vertices.Add(FVector(Top.X - Column * CharacterSize * 1.1f + (V[0] - 512.f) * Scale, Top.Y, Top.Z - Row * CharacterSize * 1.05f - (900.f - V[1]) * Scale));
		// facing back up the river, towards the piano
		Normals.Add(FVector::YAxisVector);
		AlongAcross.Add(FVector2D(V[2], V[3]));
		Order.Add(FVector2D(V[4], V[5]));
		Colors.Add(FLinearColor::White);
		Tangents.Add(FProcMeshTangent(1.f, 0.f, 0.f));
	}
	// UV0: along and across the stroke (em units); UV1: writing order and the stroke's length
	Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, AlongAcross, Order, Unused, Unused, Colors, Tangents, false, false);
	MID = UMaterialInstanceDynamic::Create(InkMaterial, this);
	MID->SetScalarParameterValue(TEXT("Written"), 0.f);
	Mesh->SetMaterial(0, MID);
}

void AInkCalligraphy::AddNote(double Time, int32 Key, float Weight)
{
	Pending.Add({ Time, Weight });
}

void AInkCalligraphy::SetSong(int32 MelodyNotes, float Seconds)
{
	// finished at about 85% of a short song, or within about a minute and a half of a long one
	const float Notes = FMath::Min(MelodyNotes * 0.85f, MelodyNotes * 90.f / FMath::Max(Seconds, 1.f));
	StrokesPerNote = InkGlyphs::NumStrokes / FMath::Max(Notes, 1.f);
}

void AInkCalligraphy::Clear()
{
	Pending.Reset();
	Target = 0.f;
	Written = 0.f;
	LiftedUntil = 0.0;
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Written"), 0.f);
	}
}

void AInkCalligraphy::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Now = GetWorld()->GetTimeSeconds();

	// notes arrive in time order; every one that has sounded by now asks the brush for a little more, louder ones more
	while (Pending.Num() > 0 && Pending[0].Time <= Now)
	{
		Target = FMath::Min(Target + StrokesPerNote * (0.6f + 0.8f * Pending[0].Weight), float(InkGlyphs::NumStrokes));
		Pending.RemoveAt(0);
	}

	// A stroke is begun only once the music has asked for all of it, and then drawn through at the brush's own
	// pace, so the brush never stalls halfway along; between strokes it lifts for a moment.
	const int32 Stroke = FMath::FloorToInt(Written);
	if (Now >= LiftedUntil && Stroke < InkGlyphs::NumStrokes)
	{
		const float StrokeEnd = Stroke + 1.f;
		const bool bStarted = Written > Stroke;
		if (bStarted || Target >= StrokeEnd)
		{
			Written = FMath::Min(Written + BrushSpeed * DeltaSeconds / FMath::Max(InkGlyphs::StrokeLengths[Stroke], 1.f), StrokeEnd);
			if (Written >= StrokeEnd)
			{
				LiftedUntil = Now + LiftSeconds;
			}
			MID->SetScalarParameterValue(TEXT("Written"), Written);
		}
	}
}
