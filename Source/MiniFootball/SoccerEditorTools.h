// Editor helpers callable from Python (automation through the editor, see Scripts/).
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SoccerEditorTools.generated.h"

class UBlueprint;

UCLASS()
class USoccerEditorTools : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Adds a Blueprint interface to a Blueprint (the Python editor API has no equivalent).
	// Creates the interface function graphs and compiles. Editor only.
	UFUNCTION(BlueprintCallable, Category = "Soccer|Editor")
	static bool ImplementBlueprintInterface(UBlueprint* Blueprint, UClass* Interface);
};
