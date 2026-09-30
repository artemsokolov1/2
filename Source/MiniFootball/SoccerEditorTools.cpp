#include "SoccerEditorTools.h"
#include "Engine/Blueprint.h"
#if WITH_EDITOR
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#endif

bool USoccerEditorTools::ImplementBlueprintInterface(UBlueprint* Blueprint, UClass* Interface)
{
#if WITH_EDITOR
	if (!Blueprint || !Interface) return false;
	if (!FBlueprintEditorUtils::ImplementNewInterface(Blueprint, Interface->GetClassPathName())) return false;
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return true;
#else
	return false;
#endif
}
