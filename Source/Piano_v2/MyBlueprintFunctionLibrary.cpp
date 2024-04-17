// Fill out your copyright notice in the Description page of Project Settings.


#include "MyBlueprintFunctionLibrary.h"
#include"Runtime/Core/Public/HAL/PlatformFilemanager.h"
#include"Runtime/Core/Public/Misc/FileHelper.h"
#include"Runtime/Core/Public/Misc/Paths.h"
#include<Windows.h>
#include<iostream>
#include "Developer\DesktopPlatform\Public\IDesktopPlatform.h"
#include "Developer\DesktopPlatform\Public\DesktopPlatformModule.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"


TArray<FString> UMyBlueprintFunctionLibrary::ReadTxt(FString path)
{
	TArray<FString> resultString;

	FFileHelper::LoadFileToStringArray(resultString, *(FPaths::ProjectContentDir() + path));

	return resultString;
}

TArray<int> UMyBlueprintFunctionLibrary::StringInt(TArray<FString> strArr) {
	TArray<int> resultInt;
	for (int i = 0; i < strArr.Num(); i++)
	{
		resultInt.Add(FCString::Atoi(*strArr[i]));
	}
	return resultInt;
}

void UMyBlueprintFunctionLibrary::ExecuteCmd(FString path) {
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	ZeroMemory(&pi, sizeof(pi));
	TCHAR* szCommandLine = const_cast<TCHAR*>(*path);
	CreateProcess(NULL, szCommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

TArray<FString> UMyBlueprintFunctionLibrary::GetMIDIFiles() {
	IFileManager& fileManager = IFileManager::Get();
	TArray<FString> MIDIFileNames;
	FString path = FPaths::ProjectContentDir() + TEXT("MIDI");
	FString extension = TEXT(".mid");
	if (fileManager.DirectoryExists(*path))
	{
		fileManager.FindFiles(MIDIFileNames, *path, *extension);
	}
	return MIDIFileNames;
}

FString UMyBlueprintFunctionLibrary::OpenWindowsFile() {
	TArray<FString> OpenFileNames;
	FString extension = TEXT("*.mid*");
	IDesktopPlatform* desktopPlatform = FDesktopPlatformModule::Get();
	desktopPlatform->OpenFileDialog(nullptr, TEXT("Select File"), 
		FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()+"MIDI"), 
		TEXT(""), extension, EFileDialogFlags::None, OpenFileNames);
	if (OpenFileNames.Num() == 0)
		return TEXT("");
	return OpenFileNames[0];
}

void UMyBlueprintFunctionLibrary::SetTimeout(AActor* actor, FString functionName, float delay, int index) {
	FTimerHandle TimerHandle;
	FTimerDelegate TimerDel;
	TimerDel.BindUFunction(actor, FName(functionName), index);
	actor->GetWorldTimerManager().SetTimer(TimerHandle, TimerDel, delay, false);
}