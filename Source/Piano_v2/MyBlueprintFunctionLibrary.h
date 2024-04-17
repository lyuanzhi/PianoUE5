// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MyBlueprintFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class PIANO_V2_API UMyBlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	UFUNCTION(BlueprintCallable, Category = "CommonFunc")
		static TArray<FString> ReadTxt(FString path);

	UFUNCTION(BlueprintCallable, Category = "CommonFunc")
		static TArray<int> StringInt(TArray<FString> strArr);

	UFUNCTION(BlueprintCallable, Category = "CommonFunc")
		static void ExecuteCmd(FString path);

	UFUNCTION(BlueprintCallable, Category = "CommonFunc")
		static TArray<FString> GetMIDIFiles();

	UFUNCTION(BlueprintCallable, Category = "CommonFunc")
		static FString OpenWindowsFile();

	UFUNCTION(BlueprintCallable, Category = "CommonFunc")
		static void SetTimeout(AActor* actor, FString functionName, float delay, int index);
};
