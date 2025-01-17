﻿#pragma once

#include "Templates/SharedPointer.h"

#include "UObject/WeakObjectPtrTemplates.h"

class UWorld;

/**
 * Class that manages the current UWorld context that a level-sequence editor should use for playback
 */
class FLevelSequencePlaybackContext_SKA : public TSharedFromThis<FLevelSequencePlaybackContext_SKA>
{
public:

	FLevelSequencePlaybackContext_SKA();
	~FLevelSequencePlaybackContext_SKA();

	/**
	 * Build a world picker widget that allows the user to choose a world, and exit the auto-bind settings
	 */
	TSharedRef<SWidget> BuildWorldPickerCombo();

	/**
	 * Resolve the current world context pointer. Can never be nullptr.
	 */
	UWorld* Get() const;

	/**
	 * Resolve the current world context pointer as a base object. Can never be nullptr.
	 */
	UObject* GetAsObject() const;

	/**
	 * Retrieve all the event contexts for the current world
	 */
	TArray<UObject*> GetEventContexts() const;

	/**
	 * Compute the new playback context based on the user's current auto-bind settings.
	 * Will use the first encountered PIE or Simulate world if possible, else the Editor world as a fallback
	 */
	static UWorld* ComputePlaybackContext();

	/**
	 * Specify a new world to use as the context. Persists until the next PIE or map change event.
	 * May be null, in which case the context will be recomputed automatically
	 */
	void OverrideWith(UWorld* InNewContext);

private:

	void OnPieEvent(bool);
	void OnMapChange(uint32);

private:

	/** Mutable cached context pointer */
	mutable TWeakObjectPtr<UWorld> WeakCurrentContext;
};