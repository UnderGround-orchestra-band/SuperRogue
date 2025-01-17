﻿// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetToolsModule.h"
#include "FLevelSequenceEditorActorBinding_SKA.h"
#include "FMovieSceneSequenceEditor_LevelSequence_SKA.h"
#include "ILevelSequenceEditorToolkit_SKA.h"
#include "ILevelSequenceModule.h"
#include "ISequencerModule.h"
#include "LevelEditorViewport.h"
#include "LevelSequenceActor.h"
#include "LevelSequenceEditorActorSpawner_SKA.h"
#include "Misc/Guid.h"
#include "UObject/GCObject.h"
#include "Framework/Commands/UICommandList.h"
#include "Styling/ISlateStyle.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "Framework/Docking/TabManager.h"
#include "Toolkits/AssetEditorToolkit.h"

class FLevelSequencePlaybackContext_SKA;
struct FFrameNumber;

class AActor;
class FMenuBuilder;
class FToolBarBuilder;
class IAssetViewport;
class ISequencer;
class UActorComponent;
class ULevelSequence;
class UMovieSceneCinematicShotTrack;
class FLevelSequencePlaybackContext;
class UPrimitiveComponent;
enum class EMapChangeType : uint8;

/**
 * Implements an Editor toolkit for level sequences.
 */
class FLevelSequenceEditorToolkit_SKA
	: public ILevelSequenceEditorToolkit_SKA
	, public FGCObject
{ 
public:

	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InStyle The style set to use.
	 */
	FLevelSequenceEditorToolkit_SKA();

	
	/** Virtual destructor */
	virtual ~FLevelSequenceEditorToolkit_SKA();

public:

	/** Iterate all open level sequence editor toolkits */
	static void IterateOpenToolkits(TFunctionRef<bool(FLevelSequenceEditorToolkit_SKA&)> Iter);

	/** Called when the tab manager is changed */
	DECLARE_EVENT_OneParam(FLevelSequenceEditorToolkit_SKA, FLevelSequenceEditorToolkitOpened, FLevelSequenceEditorToolkit_SKA&);
	static FLevelSequenceEditorToolkitOpened& OnOpened();

	/** Called when the tab manager is changed */
	DECLARE_EVENT(FLevelSequenceEditorToolkit_SKA, FLevelSequenceEditorToolkitClosed);
	FLevelSequenceEditorToolkitClosed& OnClosed() { return OnClosedEvent; }

public:

	/**
	 * Initialize this asset editor.
	 *
	 * @param Mode Asset editing mode for this editor (standalone or world-centric).
	 * @param InitToolkitHost When Mode is WorldCentric, this is the level editor instance to spawn this editor within.
	 * @param LevelSequence The animation to edit.
	 * @param TrackEditorDelegates Delegates to call to create auto-key handlers for this sequencer.
	 */
	void Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULevelSequence* LevelSequence);

	/**
	 * Get the sequencer object being edited in this tool kit.
	 *
	 * @return Sequencer object.
	 */
	virtual TSharedPtr<ISequencer> GetSequencer() const override
	{
		return Sequencer;
	}

public:

	//~ FAssetEditorToolkit interface

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(LevelSequence);
	}

	virtual bool OnRequestClose() override;
	virtual bool CanFindInContentBrowser() const override;

public:

	//~ IToolkit interface
	
	
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

protected:

	/** Add default movie scene tracks for the given actor. */
	void AddDefaultTracksForActor(AActor& Actor, const FGuid Binding);
	
	/** Add a shot to a master sequence */
	void AddShot(UMovieSceneCinematicShotTrack* ShotTrack, const FString& ShotAssetName, const FString& ShotPackagePath, FFrameNumber ShotStartTime, FFrameNumber ShotEndTime, UObject* AssetToDuplicate, const FString& FirstShotAssetName);

	/** Called whenever sequencer has received focus */
	void OnSequencerReceivedFocus();

private:

	void ExtendSequencerToolbar(FToolBarBuilder& ToolbarBuilder);

	/** Callback for executing the Add Component action. */
	void HandleAddComponentActionExecute(UActorComponent* Component);

	/** Create a new binding for the specified skeletal mesh component's animation instance. */
	void BindAnimationInstance(USkeletalMeshComponent* SkeletalComponent);

	/** Callback for map changes. */
	void HandleMapChanged(UWorld* NewWorld, EMapChangeType MapChangeType);

	/** Callback for when a master sequence is created. */
	void HandleMasterSequenceCreated(UObject* MasterSequenceAsset);

	/** Callback for the menu extensibility manager. */
	TSharedRef<FExtender> HandleMenuExtensibilityGetExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects);

	/** Callback for spawning tabs. */
	TSharedRef<SDockTab> HandleTabManagerSpawnTab(const FSpawnTabArgs& Args);

	/** Callback for the track menu extender. */
	void HandleTrackMenuExtensionAddTrack(FMenuBuilder& AddTrackMenuBuilder, TArray<UObject*> ContextObjects);

	/** Callback for actor added to sequencer. */
	void HandleActorAddedToSequencer(AActor* Actor, const FGuid Binding);

	/** Callback for VR Editor mode exiting */
	void HandleVREditorModeExit();


	/** Level sequence for our edit operation. */
	ULevelSequence* LevelSequence;

	/** Event that is cast when this toolkit is closed */
	FLevelSequenceEditorToolkitClosed OnClosedEvent;

	/** The sequencer used by this editor. */
	TSharedPtr<ISequencer> Sequencer;

	FDelegateHandle SequencerExtenderHandle;

	/** Pointer to the style set to use for toolkits. */
	//TSharedRef<ISlateStyle> Style;

	/** Instance of a class used for managing the playback context for a level sequence. */
	TSharedPtr<FLevelSequencePlaybackContext_SKA> PlaybackContext;

	/**	The tab ids for all the tabs used */
	static const FName SequencerMainTabId;
	
protected:

	/** Register sequencer editor object bindings */
	void RegisterEditorObjectBindings()
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		ActorBindingDelegateHandle = SequencerModule.RegisterEditorObjectBinding(FOnCreateEditorObjectBinding::CreateStatic(&FLevelSequenceEditorToolkit_SKA::OnCreateActorBinding));
	}

	/** Register level sequence object spawner */
	void RegisterEditorActorSpawner()
	{
		ILevelSequenceModule& LevelSequenceModule = FModuleManager::LoadModuleChecked<ILevelSequenceModule>("LevelSequence");
		EditorActorSpawnerDelegateHandle = LevelSequenceModule.RegisterObjectSpawner(FOnCreateMovieSceneObjectSpawner::CreateStatic(&FLevelSequenceEditorActorSpawner_SKA::CreateObjectSpawner));
	}



	void RegisterSequenceEditor()
	{
		ISequencerModule& SequencerModule = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer");
		SequenceEditorHandle = SequencerModule.RegisterSequenceEditor(ULevelSequence::StaticClass(), MakeUnique<FMovieSceneSequenceEditor_LevelSequence_SKA>());
	}

protected:

	/** Unregisters sequencer editor object bindings */
	void UnregisterEditorActorSpawner()
	{
		ILevelSequenceModule* LevelSequenceModule = FModuleManager::GetModulePtr<ILevelSequenceModule>("LevelSequence");
		if (LevelSequenceModule)
		{
			LevelSequenceModule->UnregisterObjectSpawner(EditorActorSpawnerDelegateHandle);
		}
	}

	/** Unregisters sequencer editor object bindings */
	void UnregisterEditorObjectBindings()
	{
		ISequencerModule* SequencerModule = FModuleManager::GetModulePtr<ISequencerModule>("Sequencer");
		if (SequencerModule)
		{
			SequencerModule->UnRegisterEditorObjectBinding(ActorBindingDelegateHandle);
		}
	}

	

	


	

	

	
protected:

	/** Callback for creating a new level sequence asset in the level. */
	static void OnCreateActorInLevel()
	{
		// Create a new level sequence
		IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UObject* NewAsset = nullptr;

		// Attempt to create a new asset
		for (TObjectIterator<UClass> It ; It ; ++It)
		{
			UClass* CurrentClass = *It;
			if (CurrentClass->IsChildOf(UFactory::StaticClass()) && !(CurrentClass->HasAnyClassFlags(CLASS_Abstract)))
			{
				UFactory* Factory = Cast<UFactory>(CurrentClass->GetDefaultObject());
				if (Factory->CanCreateNew() && Factory->ImportPriority >= 0 && Factory->SupportedClass == ULevelSequence::StaticClass())
				{
					NewAsset = AssetTools.CreateAssetWithDialog(ULevelSequence::StaticClass(), Factory);
					break;
				}
			}
		}

		if (!NewAsset)
		{
			return;
		}

		// Spawn an actor at the origin, and either move infront of the camera or focus camera on it (depending on the viewport) and open for edit
		UActorFactory* ActorFactory = GEditor->FindActorFactoryForActorClass(ALevelSequenceActor::StaticClass());
		if (!ensure(ActorFactory))
		{
			return;
		}

		AActor* Actor = GEditor->UseActorFactory(ActorFactory, FAssetData(NewAsset), &FTransform::Identity);
		if (Actor == nullptr)
		{
			return;
		}
		ALevelSequenceActor* NewActor = CastChecked<ALevelSequenceActor>(Actor);
		if (GCurrentLevelEditingViewportClient != nullptr && GCurrentLevelEditingViewportClient->IsPerspective())
		{
			GEditor->MoveActorInFrontOfCamera(*NewActor, GCurrentLevelEditingViewportClient->GetViewLocation(), GCurrentLevelEditingViewportClient->GetViewRotation().Vector());
		}
		else
		{
			GEditor->MoveViewportCamerasToActor(*NewActor, false);
		}

		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAsset);
	}

	
	

	static TSharedRef<ISequencerEditorObjectBinding> OnCreateActorBinding(TSharedRef<ISequencer> InSequencer)
	{
		return MakeShareable(new FLevelSequenceEditorActorBinding_SKA(InSequencer));
	}

	

	virtual FString GetReferencerName() const override
	{
		return "FLevelSequenceEditorModule";
	}

private:
	

	FDelegateHandle ActorBindingDelegateHandle;

	FDelegateHandle EditorActorSpawnerDelegateHandle;

	USequencerSettings* Settings;

	FDelegateHandle SequenceEditorHandle;
	
};
