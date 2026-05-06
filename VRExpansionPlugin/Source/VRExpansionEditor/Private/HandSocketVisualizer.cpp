// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

#include "HandSocketVisualizer.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "SceneManagement.h"
//#include "UObject/Field.h"
#include "VRBPDatatypes.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "EditorViewportClient.h"
#include "Components/PoseableMeshComponent.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
//#include "Persona.h"

IMPLEMENT_HIT_PROXY(HHandSocketVisProxy, HComponentVisProxy);
#define LOCTEXT_NAMESPACE "HandSocketVisualizer"

namespace
{
bool TryApplyExtendedPoseDelta(
	UHandSocketComponent* Component,
	FName BoneName,
	const FQuat& DeltaRotate,
	const FVector& DeltaTranslate
)
{
	if (!Component || BoneName == NAME_None)
	{
		return false;
	}

	FArrayProperty* ExtendedDeltasProperty = FindFProperty<FArrayProperty>(
		Component->GetClass(),
		TEXT("CustomPoseDeltasExtended")
	);
	if (!ExtendedDeltasProperty)
	{
		return false;
	}

	FStructProperty* DeltaStructProperty = CastField<FStructProperty>(ExtendedDeltasProperty->Inner);
	if (!DeltaStructProperty || !DeltaStructProperty->Struct)
	{
		return false;
	}

	FNameProperty* BoneNameProperty = FindFProperty<FNameProperty>(DeltaStructProperty->Struct, TEXT("BoneName"));
	FStructProperty* RotationProperty = FindFProperty<FStructProperty>(DeltaStructProperty->Struct, TEXT("Rotation"));
	FStructProperty* TranslationProperty = FindFProperty<FStructProperty>(DeltaStructProperty->Struct, TEXT("Translation"));
	if (!BoneNameProperty || !RotationProperty || !TranslationProperty)
	{
		return false;
	}

	if (
		RotationProperty->Struct != TBaseStructure<FRotator>::Get() ||
		TranslationProperty->Struct != TBaseStructure<FVector>::Get()
	)
	{
		return false;
	}

	void* ArrayContainer = ExtendedDeltasProperty->ContainerPtrToValuePtr<void>(Component);
	FScriptArrayHelper ArrayHelper(ExtendedDeltasProperty, ArrayContainer);

	auto ApplyDeltaToEntry = [&](void* EntryPtr)
	{
		FRotator* RotationPtr = RotationProperty->ContainerPtrToValuePtr<FRotator>(EntryPtr);
		FVector* TranslationPtr = TranslationProperty->ContainerPtrToValuePtr<FVector>(EntryPtr);
		if (!RotationPtr || !TranslationPtr)
		{
			return;
		}

		if (!DeltaRotate.IsIdentity())
		{
			FQuat UpdatedRotation = DeltaRotate * RotationPtr->Quaternion();
			UpdatedRotation.Normalize();
			*RotationPtr = UpdatedRotation.Rotator();
		}

		if (!DeltaTranslate.IsNearlyZero())
		{
			*TranslationPtr += DeltaTranslate;
		}
	};

	for (int32 Index = 0; Index < ArrayHelper.Num(); ++Index)
	{
		void* EntryPtr = ArrayHelper.GetRawPtr(Index);
		if (!EntryPtr)
		{
			continue;
		}

		FName* EntryBoneNamePtr = BoneNameProperty->ContainerPtrToValuePtr<FName>(EntryPtr);
		if (!EntryBoneNamePtr || *EntryBoneNamePtr != BoneName)
		{
			continue;
		}

		ApplyDeltaToEntry(EntryPtr);
		return true;
	}

	ArrayHelper.AddValues(1);
	void* NewEntryPtr = ArrayHelper.GetRawPtr(ArrayHelper.Num() - 1);
	if (!NewEntryPtr)
	{
		return false;
	}

	FName* NewEntryBoneNamePtr = BoneNameProperty->ContainerPtrToValuePtr<FName>(NewEntryPtr);
	if (!NewEntryBoneNamePtr)
	{
		return false;
	}

	*NewEntryBoneNamePtr = BoneName;
	if (FRotator* RotationPtr = RotationProperty->ContainerPtrToValuePtr<FRotator>(NewEntryPtr))
	{
		*RotationPtr = FRotator::ZeroRotator;
	}
	if (FVector* TranslationPtr = TranslationProperty->ContainerPtrToValuePtr<FVector>(NewEntryPtr))
	{
		*TranslationPtr = FVector::ZeroVector;
	}

	ApplyDeltaToEntry(NewEntryPtr);
	return true;
}
} // namespace

bool FHandSocketVisualizer::VisProxyHandleClick(FEditorViewportClient* InViewportClient, HComponentVisProxy* VisProxy, const FViewportClick& Click)
{
	bool bEditing = false;
	if (VisProxy && VisProxy->Component.IsValid())
	{
		bEditing = true;
		if (VisProxy->IsA(HHandSocketVisProxy::StaticGetType()))
		{

			if( const UHandSocketComponent * HandComp = UpdateSelectedHandComponent(VisProxy))
			{
				HHandSocketVisProxy* Proxy = (HHandSocketVisProxy*)VisProxy;
				if (Proxy)
				{
					CurrentlySelectedBone = Proxy->TargetBoneName;
					CurrentlySelectedBoneIdx = Proxy->BoneIdx;
					TargetViewport = InViewportClient->Viewport;
				}
			}
		}
	}

	return bEditing;
}


bool FHandSocketVisualizer::GetCustomInputCoordinateSystem(const FEditorViewportClient* ViewportClient, FMatrix& OutMatrix) const
{
	if (TargetViewport == nullptr || TargetViewport != ViewportClient->Viewport)
	{
		return false;
	}

	if (HandPropertyPath.IsValid() && CurrentlySelectedBone != NAME_None/* && CurrentlySelectedBone != "HandSocket"*/)
	{
		if (CurrentlySelectedBone == "HandSocket")
		{
			UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent();
			if (CurrentlyEditingComponent)
			{
				if (CurrentlyEditingComponent->bMirrorVisualizationMesh)
				{
					FTransform NewTrans = CurrentlyEditingComponent->GetRelativeTransform();
					NewTrans.Mirror(CurrentlyEditingComponent->GetAsEAxis(CurrentlyEditingComponent->MirrorAxis), CurrentlyEditingComponent->GetAsEAxis(CurrentlyEditingComponent->FlipAxis));

					if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
					{
						NewTrans = NewTrans * ParentComp->GetComponentTransform();
					}

					OutMatrix = FRotationMatrix::Make(NewTrans.GetRotation());
				}
			}

			return false;
		}
		else if (CurrentlySelectedBone == "Visualizer")
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{

				FTransform newTrans = FTransform::Identity;
				if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
				{
					if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
					{
						newTrans = CurrentlyEditingComponent->HandRelativePlacement * ParentComp->GetComponentTransform();
					}
				}
				else
				{
					newTrans = CurrentlyEditingComponent->GetHandRelativePlacement() * CurrentlyEditingComponent->GetComponentTransform();
				}

				OutMatrix = FRotationMatrix::Make(newTrans.GetRotation());
			}
		}
		else
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				if (IsValid(CurrentlyEditingComponent->HandVisualizerComponent))
				{
					FTransform newTrans = CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx);
					FQuat Rot = newTrans.GetRotation();
					if (!newTrans.GetRotation().ContainsNaN())
					{
						Rot.Normalize();
						OutMatrix = FRotationMatrix::Make(Rot);
						return true;
					}
				}

				return false;
			}
		}

		return true;
	}

	return false;
}

bool FHandSocketVisualizer::IsVisualizingArchetype() const
{
	return (HandPropertyPath.IsValid() && HandPropertyPath.GetParentOwningActor() && FActorEditorUtils::IsAPreviewOrInactiveActor(HandPropertyPath.GetParentOwningActor()));
}

void FHandSocketVisualizer::DrawVisualizationHUD(const UActorComponent* Component, const FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{
	if (TargetViewport == nullptr || TargetViewport != Viewport)
	{
		return;
	}

	if (const UHandSocketComponent* HandComp = Cast<const UHandSocketComponent>(Component))
	{
		if (CurrentlySelectedBone != NAME_None)
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				if (!IsValid(CurrentlyEditingComponent->HandVisualizerComponent))
				{
					return;
				}

				int32 XL;
				int32 YL;
				const FIntRect CanvasRect = Canvas->GetViewRect();

				FPlane location = View->Project(CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx).GetLocation());
				StringSize(GEngine->GetLargeFont(), XL, YL, *CurrentlySelectedBone.ToString());
				//const float DrawPositionX = location.X - XL;
				//const float DrawPositionY = location.Y - YL;
				const float DrawPositionX = FMath::FloorToFloat(CanvasRect.Min.X + (CanvasRect.Width() - XL) * 0.5f);
				const float DrawPositionY = CanvasRect.Min.Y + 50.0f;
				Canvas->DrawShadowedString(DrawPositionX, DrawPositionY, *CurrentlySelectedBone.ToString(), GEngine->GetLargeFont(), FLinearColor::Yellow);
				
			}
		}
	}
}

void FHandSocketVisualizer::DrawVisualization(const UActorComponent* Component, const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	//UWorld* World = Component->GetWorld();
	//return World && (World->WorldType == EWorldType::EditorPreview || World->WorldType == EWorldType::Inactive);

	//cast the component into the expected component type
	if (const UHandSocketComponent* HandComponent = Cast<UHandSocketComponent>(Component))
	{
		if (!HandComponent->HandVisualizerComponent)
			return;

		//This is an editor only uproperty of our targeting component, that way we can change the colors if we can't see them against the background
		const FLinearColor SelectedColor = FLinearColor::Yellow;//TargetingComponent->EditorSelectedColor;
		const FLinearColor UnselectedColor = FLinearColor::White;//TargetingComponent->EditorUnselectedColor;
		const auto ComputeHandleSize = [View](const FVector& InLocation, float BaseSize, float MinScale = 0.6f, float MaxScale = 1.0f)
		{
			const float Distance = FVector::Dist(View->ViewLocation, InLocation);
			const float DistanceAlpha = FMath::Clamp(Distance / 2500.0f, 0.0f, 1.0f);
			const float Scale = FMath::Lerp(MaxScale, MinScale, DistanceAlpha);
			return BaseSize * Scale;
		};

		const FVector Location = HandComponent->HandVisualizerComponent->GetComponentLocation();
		HHandSocketVisProxy* newHitProxy = new HHandSocketVisProxy(Component);
		newHitProxy->TargetBoneName = "Visualizer";
		const bool bVisualizerSelected = (CurrentlySelectedBone == newHitProxy->TargetBoneName);
		PDI->SetHitProxy(newHitProxy);
		PDI->DrawPoint(Location, bVisualizerSelected ? SelectedColor : FLinearColor::Red, ComputeHandleSize(Location, 24.f) + (bVisualizerSelected ? 4.f : 0.f), SDPG_Foreground);
		PDI->SetHitProxy(NULL);
		newHitProxy = nullptr;

		newHitProxy = new HHandSocketVisProxy(Component);
		newHitProxy->TargetBoneName = "HandSocket";
		const bool bSocketSelected = (CurrentlySelectedBone == newHitProxy->TargetBoneName);
		const FVector SocketLocation = HandComponent->GetComponentLocation();
		PDI->SetHitProxy(newHitProxy);
		PDI->DrawPoint(SocketLocation, bSocketSelected ? SelectedColor : FLinearColor::Green, ComputeHandleSize(SocketLocation, 22.f) + (bSocketSelected ? 4.f : 0.f), SDPG_Foreground);
		PDI->SetHitProxy(NULL);
		newHitProxy = nullptr;

		if (HandComponent->bUseCustomPoseDeltas)
		{
			TArray<FTransform> BoneTransforms = HandComponent->HandVisualizerComponent->GetBoneSpaceTransforms();
			FTransform ParentTrans = HandComponent->HandVisualizerComponent->GetComponentTransform();
			// We skip root bone, moving the visualizer itself handles that
			for (int i = 1; i < HandComponent->HandVisualizerComponent->GetNumBones(); i++)
			{

				FName BoneName = HandComponent->HandVisualizerComponent->GetBoneName(i);

				if (HandComponent->bFilterBonesByPostfix)
				{
					if (BoneName.ToString().Right(2) != HandComponent->FilterPostfix)
					{
						// Skip visualizing this bone its the incorrect side
						continue;
					}
				}

				if (HandComponent->BonesToSkip.Contains(BoneName))
				{
					// Skip visualizing this bone as its in the ignore array
					continue;
				}

				FTransform BoneTransform = HandComponent->HandVisualizerComponent->GetBoneTransform(i);
				FVector BoneLoc = BoneTransform.GetLocation();
				newHitProxy = new HHandSocketVisProxy(Component);
				newHitProxy->TargetBoneName = BoneName;
				newHitProxy->BoneIdx = i;
				const bool bBoneSelected = (CurrentlySelectedBone == newHitProxy->TargetBoneName);
				PDI->SetHitProxy(newHitProxy);
				PDI->DrawPoint(BoneLoc, bBoneSelected ? SelectedColor : UnselectedColor, ComputeHandleSize(BoneLoc, 18.f, 0.55f, 0.95f) + (bBoneSelected ? 3.f : 0.f), SDPG_Foreground);
				PDI->SetHitProxy(NULL);
				newHitProxy = nullptr;
			}
		}

		if (HandComponent->bShowRangeVisualization)
		{
			float RangeVisualization = HandComponent->OverrideDistance;

			if (RangeVisualization <= 0.0f)
			{
				if (USceneComponent* Parent = Cast<USceneComponent>(HandComponent->GetAttachParent()))
				{
					FStructProperty* ObjectProperty = CastField<FStructProperty>(Parent->GetClass()->FindPropertyByName("VRGripInterfaceSettings"));

					AActor* ParentsActor = nullptr;
					if (!ObjectProperty)
					{
						ParentsActor = Parent->GetOwner();
						if (ParentsActor)
						{
							ObjectProperty = CastField<FStructProperty>(Parent->GetOwner()->GetClass()->FindPropertyByName("VRGripInterfaceSettings"));
						}
					}

					if (ObjectProperty)
					{
						UObject* Target = ParentsActor;

						if (Target == nullptr)
						{
							Target = Parent;
						}

						if (const FBPInterfaceProperties* Curve = ObjectProperty->ContainerPtrToValuePtr<FBPInterfaceProperties>(Target))
						{
							if (HandComponent->SlotPrefix == "VRGripS")
							{
								RangeVisualization = Curve->SecondarySlotRange;
							}
							else
							{
								RangeVisualization = Curve->PrimarySlotRange;
							}
						}
					}
				}
			}

			// Scale into our parents space as that is actually what the range is based on			
			FBox BoxToDraw = FBox::BuildAABB(FVector::ZeroVector, FVector(RangeVisualization) * HandComponent->GetAttachParent()->GetComponentScale());
			BoxToDraw.Min += HandComponent->GetComponentLocation();
			BoxToDraw.Max += HandComponent->GetComponentLocation();

			DrawWireBox(PDI, BoxToDraw, FColor::Green, 0.0f);
		}
	}
}

bool FHandSocketVisualizer::GetWidgetLocation(const FEditorViewportClient* ViewportClient, FVector& OutLocation) const
{
	if (TargetViewport == nullptr || TargetViewport != ViewportClient->Viewport)
	{
		return false;
	}

	if (HandPropertyPath.IsValid() && CurrentlySelectedBone != NAME_None && CurrentlySelectedBone != "HandSocket")
	{
		if (CurrentlySelectedBone == "HandSocket")
		{
			return false;
		}
		else if (CurrentlySelectedBone == "Visualizer")
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				FTransform newTrans = FTransform::Identity;
				if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
				{
					if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
					{
						newTrans = CurrentlyEditingComponent->HandRelativePlacement * ParentComp->GetComponentTransform();
					}
				}
				else
				{
					newTrans = CurrentlyEditingComponent->GetHandRelativePlacement() * CurrentlyEditingComponent->GetComponentTransform();
				}

				OutLocation = newTrans.GetLocation();
			}
		}
		else
		{
			if (UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent())
			{
				if (IsValid(CurrentlyEditingComponent->HandVisualizerComponent))
				{
					OutLocation = CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx).GetLocation();
				}
				else
				{
					return false;
				}
			}
		}

		return true;
	}

	return false;
}

bool FHandSocketVisualizer::HandleInputDelta(FEditorViewportClient* ViewportClient, FViewport* Viewport, FVector& DeltaTranslate, FRotator& DeltaRotate, FVector& DeltaScale)
{

	if (TargetViewport == nullptr || TargetViewport != Viewport)
	{
		return false;
	}

	bool bHandled = false;

	if (HandPropertyPath.IsValid())
	{
		if (CurrentlySelectedBone == "HandSocket" || CurrentlySelectedBone == NAME_None)
		{
			bHandled = false;
		}
		else if (CurrentlySelectedBone == "Visualizer")
		{
			const FScopedTransaction Transaction(LOCTEXT("ChangingComp", "ChangingComp"));

			UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent();
			if (!CurrentlyEditingComponent)
			{
				return false;
			}

			CurrentlyEditingComponent->Modify();
			if (AActor* Owner = CurrentlyEditingComponent->GetOwner())
			{
				Owner->Modify();
			}
			bool bLevelEdit = ViewportClient->IsLevelEditorClient();

			FTransform CurrentTrans = FTransform::Identity;

			if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
			{
				if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
				{
					CurrentTrans = CurrentlyEditingComponent->HandRelativePlacement * ParentComp->GetComponentTransform();
				}
			}
			else
			{
				CurrentTrans = CurrentlyEditingComponent->GetHandRelativePlacement() * CurrentlyEditingComponent->GetComponentTransform();
			}

			if (!DeltaTranslate.IsNearlyZero())
			{
				CurrentTrans.AddToTranslation(DeltaTranslate);
			}

			if (!DeltaRotate.IsNearlyZero())
			{
				CurrentTrans.SetRotation(DeltaRotate.Quaternion() * CurrentTrans.GetRotation());
			}

			if (!DeltaScale.IsNearlyZero())
			{
				CurrentTrans.MultiplyScale3D(DeltaScale);
			}

			if (CurrentlyEditingComponent->bDecoupleMeshPlacement)
			{
				if (USceneComponent* ParentComp = CurrentlyEditingComponent->GetAttachParent())
				{
					CurrentlyEditingComponent->HandRelativePlacement = CurrentTrans.GetRelativeTransform(ParentComp->GetComponentTransform());
				}
			}
			else
			{
				CurrentlyEditingComponent->HandRelativePlacement = CurrentTrans.GetRelativeTransform(CurrentlyEditingComponent->GetComponentTransform());
			}

			NotifyPropertyModified(CurrentlyEditingComponent, FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement)));
			//GEditor->RedrawLevelEditingViewports(true);
			bHandled = true;

		}
		else
		{
			UHandSocketComponent* CurrentlyEditingComponent = GetCurrentlyEditingComponent();
			if (!CurrentlyEditingComponent || !CurrentlyEditingComponent->HandVisualizerComponent)
			{
				return false;
			}

			const FScopedTransaction Transaction(LOCTEXT("ChangingComp", "ChangingComp"));

			CurrentlyEditingComponent->Modify();
			if (AActor* Owner = CurrentlyEditingComponent->GetOwner())
			{
				Owner->Modify();
			}
			bool bLevelEdit = ViewportClient->IsLevelEditorClient();
		
			FTransform BoneTrans = CurrentlyEditingComponent->HandVisualizerComponent->GetBoneTransform(CurrentlySelectedBoneIdx);
			FTransform NewTrans = BoneTrans;
			NewTrans.SetRotation(DeltaRotate.Quaternion() * NewTrans.GetRotation());

			FQuat DeltaRotateMod = NewTrans.GetRelativeTransform(BoneTrans).GetRotation();
			bool bFoundBone = false;
			for (FBPVRHandPoseBonePair& BonePair : CurrentlyEditingComponent->CustomPoseDeltas)
			{
				if (BonePair.BoneName == CurrentlySelectedBone)
				{
					bFoundBone = true;
					BonePair.DeltaPose *= DeltaRotateMod;
					break;
				}
			}

			if (!bFoundBone)
			{
				FBPVRHandPoseBonePair newBonePair;
				newBonePair.BoneName = CurrentlySelectedBone;
				newBonePair.DeltaPose *= DeltaRotateMod;
				CurrentlyEditingComponent->CustomPoseDeltas.Add(newBonePair);
				bFoundBone = true;
			}

			if (bFoundBone)
			{
				NotifyPropertyModified(CurrentlyEditingComponent, FindFProperty<FProperty>(UHandSocketComponent::StaticClass(), GET_MEMBER_NAME_CHECKED(UHandSocketComponent, CustomPoseDeltas)));
			}

			const FVector DeltaTranslateInComponentSpace = DeltaTranslate.IsNearlyZero()
				                                               ? FVector::ZeroVector
				                                               : CurrentlyEditingComponent
					                                                 ->HandVisualizerComponent
					                                                 ->GetComponentTransform()
					                                                 .InverseTransformVectorNoScale(DeltaTranslate);
			const bool bExtendedDeltaChanged = TryApplyExtendedPoseDelta(
				CurrentlyEditingComponent,
				CurrentlySelectedBone,
				DeltaRotateMod,
				DeltaTranslateInComponentSpace
			);
			if (bExtendedDeltaChanged)
			{
				if (FProperty* ExtendedProperty = FindFProperty<FProperty>(CurrentlyEditingComponent->GetClass(), TEXT("CustomPoseDeltasExtended")))
				{
					NotifyPropertyModified(CurrentlyEditingComponent, ExtendedProperty);
				}
			}

			//GEditor->RedrawLevelEditingViewports(true);
			bHandled = true;
		}
	}

	return bHandled;
}

void FHandSocketVisualizer::EndEditing()
{
	HandPropertyPath = FComponentPropertyPath();
	CurrentlySelectedBone = NAME_None;
	CurrentlySelectedBoneIdx = INDEX_NONE;
	TargetViewport = nullptr;
}

#undef LOCTEXT_NAMESPACE
