﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "SkillEditorPreviewClient.h"


void SkillEditorPreviewClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	FEditorViewportClient::Draw(InViewport, Canvas);
}

void SkillEditorPreviewClient::Tick(float DeltaSeconds)
{
	FEditorViewportClient::Tick(DeltaSeconds);
	if(DeltaSeconds>0.f)
	{
		PreviewScene->GetWorld()->Tick(LEVELTICK_All,DeltaSeconds);
	}
}

UWorld* SkillEditorPreviewClient::GetWorld() const
{
	if(PreviewScene!=nullptr)
		return PreviewScene->GetWorld();
	return GWorld;
}

SkillEditorPreviewClient::SkillEditorPreviewClient(FEditorModeTools* InModeTools,
                                                   FPreviewScene* InPreviewScene,
                                                   const TWeakPtr<SEditorViewport>& InEditorViewportWidget ):
FEditorViewportClient(InModeTools,InPreviewScene,InEditorViewportWidget)
{
	
	SetViewLocation(FVector(100,100,100));
}

SkillEditorPreviewClient::~SkillEditorPreviewClient()
{
}
