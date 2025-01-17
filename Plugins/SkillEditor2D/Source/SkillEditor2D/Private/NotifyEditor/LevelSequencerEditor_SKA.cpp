﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "NotifyEditor/LevelSequencerEditor_SKA.h"
#include "PlayBackContext_SKA.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "EngineGlobals.h"
#include "AssetData.h"
#include "Components/PrimitiveComponent.h"
#include "Editor.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Containers/ArrayBuilder.h"
#include "Modules/ModuleManager.h"
#include "KeyParams.h"
#include "MovieSceneSequence.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBox.h"
#include "Engine/Selection.h"
//#include "LevelSequenceEditorModule.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "ULevelSequenceEditorSettings_SKA.h"
#include "FLevelSequenceEditorSpawnRegister_SKA.h"
#include "LevelSequenceEditorHelpers_SKA.h"
#include "LevelEditor.h"
#include "LevelEditorViewport.h"
#include "CineCameraActor.h"
#include "Styling/SlateIconFinder.h"
#include "KeyPropertyParams.h"
#include "ISequencer.h"
#include "ISequencerModule.h"
#include "LevelSequencePlayer.h"
#include "LevelSequenceActor.h"
#include "Sections/MovieSceneCameraCutSection.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Sections/MovieSceneSubSection.h"
#include "Tracks/MovieSceneSubTrack.h"
#include "Tracks/MovieSceneCinematicShotTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "MovieSceneToolsProjectSettings.h"
#include "MovieSceneToolHelpers.h"
#include "ScopedTransaction.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "Widgets/Docking/SDockTab.h"
#include "SequencerSettings.h"
#include "LevelEditorSequencerIntegration.h"
#include "ULevelSequenceEditorBlueprintLibrary_SKA.h"
#include "MovieSceneCaptureDialogModule.h"
#include "MovieScene.h"
#include "UnrealEdMisc.h"

// @todo sequencer: hack: setting defaults for transform tracks

#include "Sections/MovieScene3DTransformSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"

// To override Sequencer editor behavior for VR Editor 
#include "EditorWorldExtension.h"
#include "VREditorMode.h"
#include "VRModeSettings.h"


#define LOCTEXT_NAMESPACE "LevelSequenceEditor"


/* Local constants
 *****************************************************************************/

const FName FLevelSequenceEditorToolkit_SKA::SequencerMainTabId(TEXT("Sequencer_SequencerMain"));

namespace SequencerDefs
{
	static const FName SequencerAppIdentifier(TEXT("SequencerApp"));
}

static TArray<FLevelSequenceEditorToolkit_SKA*> OpenToolkits;

void FLevelSequenceEditorToolkit_SKA::IterateOpenToolkits(TFunctionRef<bool(FLevelSequenceEditorToolkit_SKA&)> Iter)
{
	for (FLevelSequenceEditorToolkit_SKA* Toolkit : OpenToolkits)
	{
		if (!Iter(*Toolkit))
		{
			return;
		}
	}
}

FLevelSequenceEditorToolkit_SKA::FLevelSequenceEditorToolkitOpened& FLevelSequenceEditorToolkit_SKA::OnOpened()
{
	static FLevelSequenceEditorToolkitOpened OnOpenedEvent;
	return OnOpenedEvent;
}

/* FLevelSequenceEditorToolkit structors
 *****************************************************************************/

FLevelSequenceEditorToolkit_SKA::FLevelSequenceEditorToolkit_SKA()
	: LevelSequence(nullptr)
{
	// register sequencer menu extenders
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	int32 NewIndex = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().Add(
		FAssetEditorExtender::CreateRaw(this, &FLevelSequenceEditorToolkit_SKA::HandleMenuExtensibilityGetExtender));
	SequencerExtenderHandle = SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates()[NewIndex].GetHandle();


	OpenToolkits.Add(this);
}


FLevelSequenceEditorToolkit_SKA::~FLevelSequenceEditorToolkit_SKA()
{
	OpenToolkits.Remove(this);

	FLevelEditorSequencerIntegration::Get().RemoveSequencer(Sequencer.ToSharedRef());

	Sequencer->Close();

	// unregister delegates
	if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
	{
		auto& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		LevelEditorModule.OnMapChanged().RemoveAll(this);
	}

	// if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelSequenceEditor")))
	// {
	// 	auto& LevelSequenceEditorModule = FModuleManager::LoadModuleChecked<ILevelSequenceEditorModule>(TEXT("LevelSequenceEditor"));
	// 	LevelSequenceEditorModule.OnMasterSequenceCreated().RemoveAll(this);
	// }

	// unregister sequencer menu extenders
	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequencerModule.GetAddTrackMenuExtensibilityManager()->GetExtenderDelegates().RemoveAll([this](const FAssetEditorExtender& Extender)
	{
		return SequencerExtenderHandle == Extender.GetHandle();
	});
}


/* FLevelSequenceEditorToolkit interface
 *****************************************************************************/

void FLevelSequenceEditorToolkit_SKA::Initialize(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULevelSequence* InLevelSequence)
{
	// create tab layout
	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_LevelSequenceEditor")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
				->Split
				(
					FTabManager::NewStack()
						->AddTab(SequencerMainTabId, ETabState::OpenedTab)
				)
		);

	LevelSequence = InLevelSequence;
	PlaybackContext = MakeShared<FLevelSequencePlaybackContext_SKA>();

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = false;

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, SequencerDefs::SequencerAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, LevelSequence);

	TSharedRef<FLevelSequenceEditorSpawnRegister_SKA> SpawnRegister = MakeShareable(new FLevelSequenceEditorSpawnRegister_SKA);

	// initialize sequencer
	FSequencerInitParams SequencerInitParams;
	{
		SequencerInitParams.RootSequence = LevelSequence;
		SequencerInitParams.bEditWithinLevelEditor = true;
		SequencerInitParams.ToolkitHost = InitToolkitHost;
		SequencerInitParams.SpawnRegister = SpawnRegister;

		SequencerInitParams.EventContexts.Bind(PlaybackContext.ToSharedRef(), &FLevelSequencePlaybackContext_SKA::GetEventContexts);
		SequencerInitParams.PlaybackContext.Bind(PlaybackContext.ToSharedRef(), &FLevelSequencePlaybackContext_SKA::GetAsObject);

		SequencerInitParams.ViewParams.UniqueName = "LevelSequenceEditor";
		SequencerInitParams.ViewParams.ScrubberStyle = ESequencerScrubberStyle::FrameBlock;
		SequencerInitParams.ViewParams.OnReceivedFocus.BindRaw(this, &FLevelSequenceEditorToolkit_SKA::OnSequencerReceivedFocus);

		SequencerInitParams.HostCapabilities.bSupportsCurveEditor = true;
		SequencerInitParams.HostCapabilities.bSupportsSaveMovieSceneAsset = true;

		TSharedRef<FExtender> ToolbarExtender = MakeShared<FExtender>();
		ToolbarExtender->AddToolBarExtension("Base Commands",
			EExtensionHook::Before, nullptr, FToolBarExtensionDelegate::
			CreateSP(this, &FLevelSequenceEditorToolkit_SKA::ExtendSequencerToolbar));
		SequencerInitParams.ViewParams.ToolbarExtender = ToolbarExtender;
	}

	Sequencer = FModuleManager::LoadModuleChecked<ISequencerModule>("Sequencer").CreateSequencer(SequencerInitParams);
	SpawnRegister->SetSequencer(Sequencer);
	Sequencer->OnActorAddedToSequencer().AddSP(this, &FLevelSequenceEditorToolkit_SKA::HandleActorAddedToSequencer);

	FLevelEditorSequencerIntegrationOptions Options;
	Options.bRequiresLevelEvents = true;
	Options.bRequiresActorEvents = true;
	Options.bCanRecord = true;

	FLevelEditorSequencerIntegration::Get().AddSequencer(Sequencer.ToSharedRef(), Options);
	ULevelSequenceEditorBlueprintLibrary_SKA::SetSequencer(Sequencer.ToSharedRef());

	// @todo remove when world-centric mode is added
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	// Reopen the scene outliner so that is refreshed with the sequencer info column
	if (Sequencer->GetSequencerSettings()->GetShowOutlinerInfoColumn())
	{
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if (LevelEditorTabManager->FindExistingLiveTab(FName("LevelEditorSceneOutliner")).IsValid())
		{
			LevelEditorTabManager->TryInvokeTab(FName("LevelEditorSceneOutliner"))->RequestCloseTab();
			LevelEditorTabManager->TryInvokeTab(FName("LevelEditorSceneOutliner"));
		}
	}
	
	// Now Attach so this window will apear in the correct front first order
	LevelEditorModule.AttachSequencer(Sequencer->GetSequencerWidget(), SharedThis(this));

	// We need to find out when the user loads a new map, because we might need to re-create puppet actors
	// when previewing a MovieScene
	LevelEditorModule.OnMapChanged().AddRaw(this, &FLevelSequenceEditorToolkit_SKA::HandleMapChanged);
	this->RegisterEditorObjectBindings();
	this->RegisterEditorActorSpawner();
	this->RegisterSequenceEditor();

	{
		UWorld* World = PlaybackContext->Get();
		UVREditorMode* VRMode = Cast<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( World )->FindExtension( UVREditorMode::StaticClass() ) );
		if (VRMode != nullptr)
		{
			VRMode->OnVREditingModeExit_Handler.BindSP(this, &FLevelSequenceEditorToolkit_SKA::HandleVREditorModeExit);
			USequencerSettings& SavedSequencerSettings = *Sequencer->GetSequencerSettings();
			VRMode->SaveSequencerSettings(Sequencer->GetKeyGroupMode() == EKeyGroupMode::KeyAll, Sequencer->GetAutoChangeMode(), SavedSequencerSettings);
			if (GetDefault<UVRModeSettings>()->bAutokeySequences)
			{
				// Override currently set auto-change behavior to always autokey
				Sequencer->SetAutoChangeMode(EAutoChangeMode::All);
				Sequencer->SetKeyGroupMode(EKeyGroupMode::KeyAll);
			}
			// Tell the VR Editor mode that Sequencer has refreshed
			VRMode->RefreshVREditorSequencer(Sequencer.Get());
		}
	}
}


/* IToolkit interface
 *****************************************************************************/

FText FLevelSequenceEditorToolkit_SKA::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Level Sequence Editor");
}


FName FLevelSequenceEditorToolkit_SKA::GetToolkitFName() const
{
	static FName SequencerName("LevelSequenceEditor");
	return SequencerName;
}


FLinearColor FLevelSequenceEditorToolkit_SKA::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.7, 0.0f, 0.0f, 0.5f);
}


FString FLevelSequenceEditorToolkit_SKA::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Sequencer ").ToString();
}


void FLevelSequenceEditorToolkit_SKA::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	// For World Centric Asset Editors this isn't called until way too late in the initialization flow
	// (ie: when you actually start to edit an asset), so the tab will be unrecognized upon restore.
	// Because of this, the Sequencer Tab Spawner is actually registered in SLevelEditor.cpp manually
	// which is early enough that you can restore the tab after an editor restart.
	if (IsWorldCentricAssetEditor())
	{
		return;
	}

	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_SequencerAssetEditor", "Sequencer"));

	InTabManager->RegisterTabSpawner(SequencerMainTabId, FOnSpawnTab::CreateSP(this, &FLevelSequenceEditorToolkit_SKA::HandleTabManagerSpawnTab))
		.SetDisplayName(LOCTEXT("SequencerMainTab", "Sequencer"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());
		//.SetIcon(FSlateIcon(Style->GetStyleSetName(), "LevelSequenceEditor.Tabs.Sequencer"));
}


void FLevelSequenceEditorToolkit_SKA::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	if (!IsWorldCentricAssetEditor())
	{
		InTabManager->UnregisterTabSpawner(SequencerMainTabId);
	}

	// @todo remove when world-centric mode is added
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	LevelEditorModule.AttachSequencer(SNullWidget::NullWidget, nullptr);
}


/* FLevelSequenceEditorToolkit implementation
 *****************************************************************************/

void FLevelSequenceEditorToolkit_SKA::ExtendSequencerToolbar(FToolBarBuilder& ToolbarBuilder)
{
	ToolbarBuilder.AddWidget(PlaybackContext->BuildWorldPickerCombo());
}

void FLevelSequenceEditorToolkit_SKA::AddDefaultTracksForActor(AActor& Actor, const FGuid Binding)
{
	// get focused movie scene
	UMovieSceneSequence* Sequence = Sequencer->GetFocusedMovieSceneSequence();

	if (Sequence == nullptr)
	{
		return;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();

	if (MovieScene == nullptr)
	{
		return;
	}

	// add default tracks
	for (const FLevelSequenceTrackSettings_SKA& TrackSettings : GetDefault<ULevelSequenceEditorSettings_SKA>()->TrackSettings)
	{
		UClass* MatchingActorClass = TrackSettings.MatchingActorClass.ResolveClass();

		if ((MatchingActorClass == nullptr) || !Actor.IsA(MatchingActorClass))
		{
			continue;
		}

		// add tracks by type
		for (const FSoftClassPath& DefaultTrack : TrackSettings.DefaultTracks)
		{
			UClass* TrackClass = DefaultTrack.ResolveClass();

			// exclude any tracks explicitly marked for exclusion
			for (const FLevelSequenceTrackSettings_SKA& ExcludeTrackSettings : GetDefault<ULevelSequenceEditorSettings_SKA>()->TrackSettings)
			{
				UClass* ExcludeMatchingActorClass = ExcludeTrackSettings.MatchingActorClass.ResolveClass();

				if ((ExcludeMatchingActorClass == nullptr) || !Actor.IsA(ExcludeMatchingActorClass))
				{
					continue;
				}
				
				for (const FSoftClassPath& ExcludeDefaultTrack : ExcludeTrackSettings.ExcludeDefaultTracks)
				{
					if (ExcludeDefaultTrack == DefaultTrack)
					{
						TrackClass = nullptr;
						break;
					}
				}				
			}

			if (TrackClass != nullptr)
			{
				UMovieSceneTrack* NewTrack = MovieScene->FindTrack(TrackClass, Binding);
				if (!NewTrack)
				{
					NewTrack = MovieScene->AddTrack(TrackClass, Binding);
				}

				bool bCreateDefaultSection = false;
#if WITH_EDITORONLY_DATA
				bCreateDefaultSection = NewTrack->SupportsDefaultSections();
#endif

				if (bCreateDefaultSection)
				{
					UMovieSceneSection* NewSection;
					if (NewTrack->GetAllSections().Num() > 0)
					{
						NewSection = NewTrack->GetAllSections()[0];
					}
					else
					{
						NewSection = NewTrack->CreateNewSection();
						NewTrack->AddSection(*NewSection);
					}

					// @todo sequencer: hack: setting defaults for transform tracks
					if (NewTrack->IsA(UMovieScene3DTransformTrack::StaticClass()) && Sequencer->GetAutoSetTrackDefaults())
					{
						auto TransformSection = Cast<UMovieScene3DTransformSection>(NewSection);

						FVector Location = Actor.GetActorLocation();
						FRotator Rotation = Actor.GetActorRotation();
						FVector Scale = Actor.GetActorScale();

						if (Actor.GetRootComponent())
						{
							FTransform ActorRelativeTransform = Actor.GetRootComponent()->GetRelativeTransform();

							Location = ActorRelativeTransform.GetTranslation();
							Rotation = ActorRelativeTransform.GetRotation().Rotator();
							Scale = ActorRelativeTransform.GetScale3D();
						}

						TArrayView<FMovieSceneFloatChannel*> FloatChannels = TransformSection->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
						FloatChannels[0]->SetDefault(Location.X);
						FloatChannels[1]->SetDefault(Location.Y);
						FloatChannels[2]->SetDefault(Location.Z);

						FloatChannels[3]->SetDefault(Rotation.Euler().X);
						FloatChannels[4]->SetDefault(Rotation.Euler().Y);
						FloatChannels[5]->SetDefault(Rotation.Euler().Z);

						FloatChannels[6]->SetDefault(Scale.X);
						FloatChannels[7]->SetDefault(Scale.Y);
						FloatChannels[8]->SetDefault(Scale.Z);
					}

					if (GetSequencer()->GetInfiniteKeyAreas())
					{
						NewSection->SetRange(TRange<FFrameNumber>::All());
					}
				}
			}
		}

		// construct a map of the properties that should be excluded per component
		TMap<UObject*, TArray<FString> > ExcludePropertyTracksMap;
		for (const FLevelSequenceTrackSettings_SKA& ExcludeTrackSettings : GetDefault<ULevelSequenceEditorSettings_SKA>()->TrackSettings)
		{
			UClass* ExcludeMatchingActorClass = ExcludeTrackSettings.MatchingActorClass.ResolveClass();

			if ((ExcludeMatchingActorClass == nullptr) || !Actor.IsA(ExcludeMatchingActorClass))
			{
				continue;
			}

			for (const FLevelSequencePropertyTrackSettings_SKA& PropertyTrackSettings : ExcludeTrackSettings.ExcludeDefaultPropertyTracks)
			{
				UObject* PropertyOwner = &Actor;

				// determine object hierarchy
				TArray<FString> ComponentNames;
				PropertyTrackSettings.ComponentPath.ParseIntoArray(ComponentNames, TEXT("."));

				for (const FString& ComponentName : ComponentNames)
				{
					PropertyOwner = FindObjectFast<UObject>(PropertyOwner, *ComponentName);

					if (PropertyOwner == nullptr)
					{
						continue;
					}
				}

				if (PropertyOwner)
				{
					TArray<FString> PropertyNames;
					PropertyTrackSettings.PropertyPath.ParseIntoArray(PropertyNames, TEXT("."));

					ExcludePropertyTracksMap.Add(PropertyOwner, PropertyNames);
				}
			}
		}

		// add tracks by property
		for (const FLevelSequencePropertyTrackSettings_SKA& PropertyTrackSettings : TrackSettings.DefaultPropertyTracks)
		{
			TSharedRef<FPropertyPath> PropertyPath = FPropertyPath::CreateEmpty();
			UObject* PropertyOwner = &Actor;

			// determine object hierarchy
			TArray<FString> ComponentNames;
			PropertyTrackSettings.ComponentPath.ParseIntoArray(ComponentNames, TEXT("."));

			for (const FString& ComponentName : ComponentNames)
			{
				PropertyOwner = FindObjectFast<UObject>(PropertyOwner, *ComponentName);

				if (PropertyOwner == nullptr)
				{
					return;
				}
			}

			UStruct* PropertyOwnerClass = PropertyOwner->GetClass();

			// determine property path
			TArray<FString> PropertyNames;
			PropertyTrackSettings.PropertyPath.ParseIntoArray(PropertyNames, TEXT("."));

			for (const FString& PropertyName : PropertyNames)
			{
				// skip past excluded properties
				if (ExcludePropertyTracksMap.Contains(PropertyOwner) && ExcludePropertyTracksMap[PropertyOwner].Contains(PropertyName))
				{
					PropertyPath = FPropertyPath::CreateEmpty();
					break;
				}

				FProperty* Property = PropertyOwnerClass->FindPropertyByName(*PropertyName);

				if (Property != nullptr)
				{
					PropertyPath->AddProperty(FPropertyInfo(Property));
				}

				FStructProperty* StructProperty = CastField<FStructProperty>(Property);

				if (StructProperty != nullptr)
				{
					PropertyOwnerClass = StructProperty->Struct;
					continue;
				}

				FObjectProperty* ObjectProperty = CastField<FObjectProperty>(Property);

				if (ObjectProperty != nullptr)
				{
					PropertyOwnerClass = ObjectProperty->PropertyClass;
					continue;
				}

				break;
			}

			if (!Sequencer->CanKeyProperty(FCanKeyPropertyParams(PropertyOwner->GetClass(), *PropertyPath)))
			{
				continue;
			}

			// key property
			FKeyPropertyParams KeyPropertyParams(TArrayBuilder<UObject*>().Add(PropertyOwner), *PropertyPath, ESequencerKeyMode::ManualKey);

			Sequencer->KeyProperty(KeyPropertyParams);
		}
	}
}


/* FLevelSequenceEditorToolkit callbacks
 *****************************************************************************/

void FLevelSequenceEditorToolkit_SKA::OnSequencerReceivedFocus()
{
	if (Sequencer.IsValid())
	{
		FLevelEditorSequencerIntegration::Get().OnSequencerReceivedFocus(Sequencer.ToSharedRef());
	}
}

void FLevelSequenceEditorToolkit_SKA::HandleAddComponentActionExecute(UActorComponent* Component)
{
	const FScopedTransaction Transaction(LOCTEXT("AddComponent", "Add Component"));

	FString ComponentName = Component->GetName();

	TArray<UActorComponent*> ActorComponents;
	ActorComponents.Add(Component);

	USelection* SelectedActors = GEditor->GetSelectedActors();
	if (SelectedActors && SelectedActors->Num() > 0)
	{
		for (FSelectionIterator Iter(*SelectedActors); Iter; ++Iter)
		{
			AActor* Actor = CastChecked<AActor>(*Iter);

			TArray<UActorComponent*> OutActorComponents;
			Actor->GetComponents(OutActorComponents);
	
			for (UActorComponent* ActorComponent : OutActorComponents)
			{
				if (ActorComponent->GetName() == ComponentName)
				{
					ActorComponents.AddUnique(ActorComponent);
				}
			}
		}
	}

	for (UActorComponent* ActorComponent : ActorComponents)
	{
		Sequencer->GetHandleToObject(ActorComponent);
	}
}


void FLevelSequenceEditorToolkit_SKA::HandleActorAddedToSequencer(AActor* Actor, const FGuid Binding)
{
	AddDefaultTracksForActor(*Actor, Binding);
}


void FLevelSequenceEditorToolkit_SKA::HandleVREditorModeExit()
{
	UWorld* World = PlaybackContext->Get();
	UVREditorMode* VRMode = CastChecked<UVREditorMode>( GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions( World )->FindExtension( UVREditorMode::StaticClass() ) );

	// Reset sequencer settings
	Sequencer->SetAutoChangeMode(VRMode->GetSavedEditorState().AutoChangeMode);
	Sequencer->SetKeyGroupMode(VRMode->GetSavedEditorState().bKeyAllEnabled ? EKeyGroupMode::KeyAll : EKeyGroupMode::KeyChanged);
	VRMode->OnVREditingModeExit_Handler.Unbind();
}

void FLevelSequenceEditorToolkit_SKA::HandleMapChanged(class UWorld* NewWorld, EMapChangeType MapChangeType)
{
	// @todo sequencer: We should only wipe/respawn puppets that are affected by the world that is being changed! (multi-UWorld support)
	if( ( MapChangeType == EMapChangeType::LoadMap || MapChangeType == EMapChangeType::NewMap || MapChangeType == EMapChangeType::TearDownWorld) )
	{
		Sequencer->GetSpawnRegister().CleanUp(*Sequencer);
		CloseWindow();
	}
}

void FLevelSequenceEditorToolkit_SKA::AddShot(UMovieSceneCinematicShotTrack* ShotTrack, const FString& ShotAssetName, const FString& ShotPackagePath, FFrameNumber ShotStartTime, FFrameNumber ShotEndTime, UObject* AssetToDuplicate, const FString& FirstShotAssetName)
{
	// Create a level sequence asset for the shot
	UObject* ShotAsset = LevelSequenceEditorHelpers_SKA::CreateLevelSequenceAsset(ShotAssetName, ShotPackagePath, AssetToDuplicate);
	UMovieSceneSequence* ShotSequence = Cast<UMovieSceneSequence>(ShotAsset);
	UMovieSceneSubSection* ShotSubSection = ShotTrack->AddSequence(ShotSequence, ShotStartTime, (ShotEndTime-ShotStartTime).Value);

	// Focus on the new shot
	GetSequencer()->ForceEvaluate();
	GetSequencer()->FocusSequenceInstance(*ShotSubSection);

	const ULevelSequenceMasterSequenceSettings_SKA* MasterSequenceSettings = GetDefault<ULevelSequenceMasterSequenceSettings_SKA>();
	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	// Create any subshots
	if (MasterSequenceSettings->SubSequenceNames.Num())
	{
		UMovieSceneSubTrack* SubTrack = Cast<UMovieSceneSubTrack>(ShotSequence->GetMovieScene()->FindMasterTrack(UMovieSceneSubTrack::StaticClass()));
		if (!SubTrack)
		{
			SubTrack = Cast<UMovieSceneSubTrack>(ShotSequence->GetMovieScene()->AddMasterTrack(UMovieSceneSubTrack::StaticClass()));
		}
	
		int32 RowIndex = 0;
		for (auto SubSequenceName : MasterSequenceSettings->SubSequenceNames)
		{
			FString SubSequenceAssetName = ShotAssetName + ProjectSettings->SubSequenceSeparator + SubSequenceName.ToString();

			UMovieSceneSequence* SubSequence = nullptr;
			if (!MasterSequenceSettings->bInstanceSubSequences || ShotTrack->GetAllSections().Num() == 1)
			{
				UObject* SubSequenceAsset = LevelSequenceEditorHelpers_SKA::CreateLevelSequenceAsset(SubSequenceAssetName, ShotPackagePath);
				SubSequence = Cast<UMovieSceneSequence>(SubSequenceAsset);
			}
			else
			{
				// Get the corresponding sequence from the first shot
				UMovieSceneSubSection* FirstShotSubSection = Cast<UMovieSceneSubSection>(ShotTrack->GetAllSections()[0]);
				UMovieSceneSequence* FirstShotSequence = FirstShotSubSection->GetSequence();
				UMovieSceneSubTrack* FirstShotSubTrack = Cast<UMovieSceneSubTrack>(FirstShotSequence->GetMovieScene()->FindMasterTrack(UMovieSceneSubTrack::StaticClass()));
			
				FString FirstShotSubSequenceAssetName = FirstShotAssetName + ProjectSettings->SubSequenceSeparator + SubSequenceName.ToString();

				for (auto Section : FirstShotSubTrack->GetAllSections())
				{
					UMovieSceneSubSection* SubSection = Cast<UMovieSceneSubSection>(Section);
					if (SubSection->GetSequence()->GetDisplayName().ToString() == FirstShotSubSequenceAssetName)
					{
						SubSequence = SubSection->GetSequence();
						break;
					}
				}
			}

			if (SubSequence != nullptr)
			{
				UMovieSceneSubSection* SubSection = SubTrack->AddSequence(SubSequence, 0, (ShotEndTime-ShotStartTime).Value);
				SubSection->SetRowIndex(RowIndex++);
			}
		}
	}

	// Create a camera cut track with a camera if it doesn't already exist
	UMovieSceneTrack* CameraCutTrack = ShotSequence->GetMovieScene()->GetCameraCutTrack();
	if (!CameraCutTrack)
	{	
		// Create a cine camera asset
		ACineCameraActor* NewCamera = GCurrentLevelEditingViewportClient->GetWorld()->SpawnActor<ACineCameraActor>();
		
		const USequencerSettings* SequencerSettings = GetDefault<USequencerSettings>();
		bool bCreateSpawnableCamera = SequencerSettings->GetCreateSpawnableCameras();

		FGuid CameraGuid;
		if (bCreateSpawnableCamera)
		{
			CameraGuid = GetSequencer()->MakeNewSpawnable(*NewCamera);
			UObject* SpawnedCamera = GetSequencer()->FindSpawnedObjectOrTemplate(CameraGuid);
			if (SpawnedCamera)
			{
				GCurrentLevelEditingViewportClient->GetWorld()->EditorDestroyActor(NewCamera, true);
				NewCamera = Cast<ACineCameraActor>(SpawnedCamera);
			}
		}
		else
		{
			CameraGuid = GetSequencer()->CreateBinding(*NewCamera, NewCamera->GetActorLabel());
		}
		NewCamera->SetActorLocation( GCurrentLevelEditingViewportClient->GetViewLocation(), false );
		NewCamera->SetActorRotation( GCurrentLevelEditingViewportClient->GetViewRotation() );
		//pNewCamera->CameraComponent->FieldOfView = ViewportClient->ViewFOV; //@todo set the focal length from this field of view
		
		AddDefaultTracksForActor(*NewCamera, CameraGuid);

		// Create a new camera cut section and add it to the camera cut track
		CameraCutTrack = ShotSequence->GetMovieScene()->AddCameraCutTrack(UMovieSceneCameraCutTrack::StaticClass());
		UMovieSceneCameraCutSection* CameraCutSection = NewObject<UMovieSceneCameraCutSection>(CameraCutTrack, NAME_None, RF_Transactional);

		CameraCutSection->SetRange(ShotSequence->GetMovieScene()->GetPlaybackRange());
		CameraCutSection->SetCameraGuid(CameraGuid);
		CameraCutTrack->AddSection(*CameraCutSection);
	}
}

void FLevelSequenceEditorToolkit_SKA::HandleMasterSequenceCreated(UObject* MasterSequenceAsset)
{
	const FScopedTransaction Transaction( LOCTEXT( "CreateMasterSequence", "Create Master Sequence" ) );
	
	const ULevelSequenceMasterSequenceSettings_SKA* MasterSequenceSettings = GetDefault<ULevelSequenceMasterSequenceSettings_SKA>();
	uint32 NumShots = MasterSequenceSettings->MasterSequenceNumShots;
	ULevelSequence* AssetToDuplicate = MasterSequenceSettings->MasterSequenceLevelSequenceToDuplicate.Get();

	const UMovieSceneToolsProjectSettings* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();

	UMovieSceneSequence* MasterSequence = Cast<UMovieSceneSequence>(MasterSequenceAsset);
	UMovieSceneCinematicShotTrack* ShotTrack = MasterSequence->GetMovieScene()->AddMasterTrack<UMovieSceneCinematicShotTrack>();

	FFrameRate TickResolution = MasterSequence->GetMovieScene()->GetTickResolution();

	// Create shots with a camera cut and a camera for each
	FFrameNumber SequenceStartTime = (ProjectSettings->DefaultStartTime * TickResolution).FloorToFrame();
	FFrameNumber ShotStartTime = SequenceStartTime;
	FFrameNumber ShotEndTime   = ShotStartTime;
	int32        ShotDuration  = (ProjectSettings->DefaultDuration * TickResolution).RoundToFrame().Value;
	FString FirstShotName; 
	for (uint32 ShotIndex = 0; ShotIndex < NumShots; ++ShotIndex)
	{
		ShotEndTime += ShotDuration;

		FString ShotName = MovieSceneToolHelpers::GenerateNewShotName(ShotTrack->GetAllSections(), ShotStartTime);
		FString ShotPackagePath = MovieSceneToolHelpers::GenerateNewShotPath(MasterSequence->GetMovieScene(), ShotName);

		if (ShotIndex == 0)
		{
			FirstShotName = ShotName;
		}

		AddShot(ShotTrack, ShotName, ShotPackagePath, ShotStartTime, ShotEndTime, AssetToDuplicate, FirstShotName);
		GetSequencer()->ResetToNewRootSequence(*MasterSequence);

		ShotStartTime = ShotEndTime;
	}

	MasterSequence->GetMovieScene()->SetPlaybackRange(SequenceStartTime, (ShotEndTime - SequenceStartTime).Value);

#if WITH_EDITORONLY_DATA
	const double SequenceStartSeconds = SequenceStartTime / TickResolution;
	const double SequenceEndSeconds   = ShotEndTime / TickResolution;
	const double OutputChange = (SequenceEndSeconds - SequenceStartSeconds) * 0.1;

	FMovieSceneEditorData& EditorData = MasterSequence->GetMovieScene()->GetEditorData();
	EditorData.ViewStart = EditorData.WorkStart = SequenceStartSeconds - OutputChange;
	EditorData.ViewEnd   = EditorData.WorkEnd   = SequenceEndSeconds + OutputChange;
#endif

	GetSequencer()->ResetToNewRootSequence(*MasterSequence);

	UActorFactory* ActorFactory = GEditor->FindActorFactoryForActorClass(ALevelSequenceActor::StaticClass());
	if (!ensure(ActorFactory))
	{
		return;
	}

	ALevelSequenceActor* NewActor = CastChecked<ALevelSequenceActor>(GEditor->UseActorFactory(ActorFactory, FAssetData(MasterSequenceAsset), &FTransform::Identity));
	if (GCurrentLevelEditingViewportClient != nullptr && GCurrentLevelEditingViewportClient->IsPerspective())
	{
		GEditor->MoveActorInFrontOfCamera(*NewActor, GCurrentLevelEditingViewportClient->GetViewLocation(), GCurrentLevelEditingViewportClient->GetViewRotation().Vector());
	}
	else
	{
		GEditor->MoveViewportCamerasToActor(*NewActor, false);
	}
}


TSharedRef<FExtender> FLevelSequenceEditorToolkit_SKA::HandleMenuExtensibilityGetExtender(const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> ContextSensitiveObjects)
{
	TSharedRef<FExtender> AddTrackMenuExtender(new FExtender());
	AddTrackMenuExtender->AddMenuExtension(
		SequencerMenuExtensionPoints::AddTrackMenu_PropertiesSection,
		EExtensionHook::Before,
		CommandList,
		FMenuExtensionDelegate::CreateRaw(this, &FLevelSequenceEditorToolkit_SKA::HandleTrackMenuExtensionAddTrack, ContextSensitiveObjects));

	return AddTrackMenuExtender;
}


TSharedRef<SDockTab> FLevelSequenceEditorToolkit_SKA::HandleTabManagerSpawnTab(const FSpawnTabArgs& Args)
{
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;

	// if (Args.GetTabId() == SequencerMainTabId)
	// {
	// 	
	// }
	if(Sequencer.IsValid())
	TabWidget = Sequencer->GetSequencerWidget();
	return SNew(SDockTab)
		.Label(LOCTEXT("SequencerMainTitle", "Sequencer"))
		.TabColorScale(GetTabColorScale())
		.TabRole(ETabRole::PanelTab)
		[
			TabWidget.ToSharedRef()
		];
}


void FLevelSequenceEditorToolkit_SKA::HandleTrackMenuExtensionAddTrack(FMenuBuilder& AddTrackMenuBuilder, TArray<UObject*> ContextObjects)
{
	if (ContextObjects.Num() != 1)
	{
		return;
	}

	AActor* Actor = Cast<AActor>(ContextObjects[0]);
	if (Actor != nullptr)
	{
		AddTrackMenuBuilder.BeginSection("Components", LOCTEXT("ComponentsSection", "Components"));
		{
			for (UActorComponent* Component : Actor->GetComponents())
			{
				if (Component)
				{
					FUIAction AddComponentAction(FExecuteAction::CreateSP(this, &FLevelSequenceEditorToolkit_SKA::HandleAddComponentActionExecute, Component));
					FText AddComponentLabel = FText::FromString(Component->GetName());
					FText AddComponentToolTip = FText::Format(LOCTEXT("ComponentToolTipFormat", "Add {0} component"), FText::FromString(Component->GetName()));
					AddTrackMenuBuilder.AddMenuEntry(AddComponentLabel, AddComponentToolTip, FSlateIcon(), AddComponentAction);
				}
			}
		}
		AddTrackMenuBuilder.EndSection();
	}
	else
	{
		if (USkeletalMeshComponent* SkeletalComponent = Cast<USkeletalMeshComponent>(ContextObjects[0]))
		{
			UAnimInstance* AnimInstance = SkeletalComponent->GetAnimInstance();
			
			FText AnimInstanceLabel = LOCTEXT("AnimInstanceLabel", "Anim Instance");
			FText DetailedInstanceText = AnimInstance
				? FText::Format(LOCTEXT("AnimInstanceLabelFormat", "Anim Instance '{0}'"), FText::FromString(AnimInstance->GetName()))
				: AnimInstanceLabel;

			AddTrackMenuBuilder.BeginSection("Anim Instance", AnimInstanceLabel);
			{
				AddTrackMenuBuilder.AddMenuEntry(
					DetailedInstanceText,
					LOCTEXT("AnimInstanceToolTip", "Add this skeletal mesh component's animation instance."),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateSP(this, &FLevelSequenceEditorToolkit_SKA::BindAnimationInstance, SkeletalComponent)
					)
				);
			}
			AddTrackMenuBuilder.EndSection();
		}
	}
}

void FLevelSequenceEditorToolkit_SKA::BindAnimationInstance(USkeletalMeshComponent* SkeletalComponent)
{
	UAnimInstance* AnimInstance = SkeletalComponent->GetAnimInstance();

	// If there is no script instance at the moment, just use a dummy instance for the purposes of setting up the binding in the first place.
	// This temporary object will get GC'd later on and is never actually applied to the anim instance
	Sequencer->GetHandleToObject(AnimInstance ? AnimInstance : NewObject<UAnimInstance>(SkeletalComponent));
}

bool FLevelSequenceEditorToolkit_SKA::OnRequestClose()
{
	UWorld* World = PlaybackContext->Get();
	UVREditorMode* VRMode = Cast<UVREditorMode>(GEditor->GetEditorWorldExtensionsManager()->GetEditorWorldExtensions(World)->FindExtension(UVREditorMode::StaticClass()));
	if (VRMode != nullptr)
	{
		// Null out the VR Mode's sequencer pointer
		VRMode->RefreshVREditorSequencer(nullptr);
	}
	OpenToolkits.Remove(this);

	OnClosedEvent.Broadcast();
	return true;
}

bool FLevelSequenceEditorToolkit_SKA::CanFindInContentBrowser() const
{
	// False so that sequencer doesn't take over Find In Content Browser functionality and always find the level sequence asset
	return false;
}

#undef LOCTEXT_NAMESPACE

