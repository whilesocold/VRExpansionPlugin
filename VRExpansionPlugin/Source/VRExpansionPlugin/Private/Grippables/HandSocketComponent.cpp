// All Rights Reserved.

#include "Grippables/HandSocketComponent.h"
#include UE_INLINE_GENERATED_CPP_BY_NAME(HandSocketComponent)

#include "CoreMinimal.h"
#include "UObject/UObjectIterator.h"
#include "Engine/CollisionProfile.h"
#include "BoneContainer.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/PoseSnapshot.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/SkinnedAsset.h"
// #include "VRExpansionFunctionLibrary.h" // Закомментировано в оригинале
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
// #include "VRGripInterface.h" // Закомментировано в оригинале
// #include "VRBPDatatypes.h" // Закомментировано в оригинале
#include "Grippables/HandSocketComponent.h"

#include "GripMotionControllerComponent.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/CustomVersion.h"

#if WITH_PUSH_MODEL
#include "Net/Core/PushModel/PushModel.h"
#endif

DEFINE_LOG_CATEGORY(LogVRHandSocketComponent);

// Вспомогательные форматтеры и дельты
static FORCEINLINE FString TStr(const FTransform& T)
{
	return FString::Printf(TEXT("%s|%s|%s"),
	                       *FString::Printf(TEXT("%.6f,%.6f,%.6f"), T.GetLocation().X, T.GetLocation().Y,
	                                        T.GetLocation().Z),
	                       *FString::Printf(TEXT("%.6f,%.6f,%.6f"), T.Rotator().Pitch, T.Rotator().Yaw,
	                                        T.Rotator().Roll),
	                       *FString::Printf(TEXT("%.6f,%.6f,%.6f"), T.GetScale3D().X, T.GetScale3D().Y,
	                                        T.GetScale3D().Z)
	);
}

static FORCEINLINE FString RotDeltaStr(const FQuat& A, const FQuat& B)
{
	const FQuat Delta = A.Inverse() * B;
	const FRotator R = Delta.Rotator();
	return FString::Printf(TEXT("ΔRot(PYR)=%.3f,%.3f,%.3f  (|Δ|≈%.3f)"),
	                       R.Pitch, R.Yaw, R.Roll, FMath::RadiansToDegrees(Delta.GetAngle()));
}

static FORCEINLINE FString LocDeltaStr(const FVector& A, const FVector& B)
{
	const FVector D = B - A;
	return FString::Printf(TEXT("ΔLoc=(%.3f,%.3f,%.3f), |Δ|=%.3f"), D.X, D.Y, D.Z, D.Size());
}

const FGuid FVRHandSocketCustomVersion::GUID(0x5A018B7F, 0x48A7AFDE, 0xAFBEB580, 0xAD575412);

// Register the custom version with core
// Регистрируем пользовательскую версию в ядре
FCustomVersionRegistration GRegisterHandSocketCustomVersion(
	FVRHandSocketCustomVersion::GUID, FVRHandSocketCustomVersion::LatestVersion, TEXT("HandSocketVer"));

void UHandSocketComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FVRHandSocketCustomVersion::GUID);

#if WITH_EDITORONLY_DATA
	const int32 CustomHandSocketVersion = Ar.CustomVer(FVRHandSocketCustomVersion::GUID);

	if (CustomHandSocketVersion < FVRHandSocketCustomVersion::HandSocketStoringSetState)
	{
		// Сохраняем состояние отсоединенности на основе старой переменной до введения трекера состояния
		bDecoupled = bDecoupleMeshPlacement;
	}
#endif
}

//=============================================================================
UHandSocketComponent::UHandSocketComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bReplicateMovement = false; // По умолчанию движение не реплицируется
	PrimaryComponentTick.bCanEverTick = false; // Компонент не требует тика по умолчанию
	PrimaryComponentTick.bStartWithTickEnabled = false; // Тик отключен при старте
	// Setting absolute scale so we don't have to care about our parents scale
	// Устанавливаем абсолютный масштаб, чтобы не зависеть от масштаба родителя
	this->SetUsingAbsoluteScale(true);
	// this->bReplicates = true; // Закомментировано в оригинале, репликация управляется через bReplicateMovement

	bRepGameplayTags = true; // По умолчанию реплицируем Gameplay теги

#if WITH_EDITORONLY_DATA
	bTickedPose = false; // Флаг для отслеживания обновления позы в редакторе
	bDecoupled = false; // Флаг отсоединенности для редактора
	bShowVisualizationMesh = true; // Показывать меш визуализации по умолчанию
	bMirrorVisualizationMesh = false; // Не отзеркаливать визуализацию по умолчанию
	bShowRangeVisualization = false; // Не показывать визуализацию диапазона по умолчанию
	bFilterBonesByPostfix = false; // Не фильтровать кости по постфиксу по умолчанию
	bUseAdvancedFullBodyPreview = true; // Включаем по умолчанию, так как это основная цель
	FilterPostfix = FString(TEXT("_r")); // Постфикс для фильтрации по умолчанию
#endif

	// Инициализация значений по умолчанию
	HandRelativePlacement = FTransform::Identity;
	bAlwaysInRange = false;
	bDisabled = false;
	bLockInPlace = false;
	bMatchRotation = false;
	OverrideDistance = 0.0f;
	SlotPrefix = FName("VRGripP"); // Префикс слота анимации по умолчанию
	bUseCustomPoseDeltas = false;
	HandTargetAnimation = nullptr;
	MirroredScale = FVector(1.f, 1.f, -1.f); // Масштаб для отзеркаливания по умолчанию
	bOnlySnapMesh = false;
	bOnlyUseHandPose = false;
	bIgnoreAttachBone = false;
	bFlipForLeftHand = false;
	bLeftHandDominant = false;
	bOnlyFlipRotation = false;

	MirrorAxis = EVRAxis::X; // Ось отзеркаливания по умолчанию
	FlipAxis = EVRAxis::Y; // Ось инвертирования по умолчанию
}

UAnimSequence* UHandSocketComponent::GetTargetAnimation()
{
	return HandTargetAnimation;
}

void UHandSocketComponent::GetAllHandSocketComponents(TArray<UHandSocketComponent*>& OutHandSockets)
{
	// Итерация по всем объектам типа UHandSocketComponent
	for (TObjectIterator<UHandSocketComponent> It; It; ++It)
	{
		UHandSocketComponent* HandSocket = *It;
		// Проверка на валидность и не является ли шаблоном
		if (IsValid(HandSocket) && !HandSocket->IsTemplate())
		{
			OutHandSockets.Add(HandSocket); // Добавляем в выходной массив
		}
	}
}

bool UHandSocketComponent::GetAllHandSocketComponentsInRange(
	FVector SearchFromWorldLocation, float SearchRange, TArray<UHandSocketComponent*>& OutHandSockets)
{
	float SearchDistSq = FMath::Square(SearchRange); // Квадрат дистанции для оптимизации

	UHandSocketComponent* HandSocket = nullptr;
	FTransform HandSocketTrans;
	// Итерация по всем объектам типа UHandSocketComponent
	for (TObjectIterator<UHandSocketComponent> It; It; ++It)
	{
		HandSocket = *It;
		// Проверка на валидность и не является ли шаблоном
		if (IsValid(HandSocket) && !HandSocket->IsTemplate())
		{
			// Получаем мировую трансформацию сокета
			HandSocketTrans = HandSocket->GetRelativeTransform() * HandSocket->GetOwner()->GetActorTransform();
			// Проверяем, находится ли сокет в пределах квадрата дистанции
			if (FVector::DistSquared(HandSocketTrans.GetLocation(), SearchFromWorldLocation) <= SearchDistSq)
			{
				OutHandSockets.Add(HandSocket); // Добавляем в выходной массив
			}
		}
	}

	// Возвращаем true, если найден хотя бы один сокет
	return OutHandSockets.Num() > 0;
}

UHandSocketComponent* UHandSocketComponent::GetClosestHandSocketComponentInRange(
	FVector SearchFromWorldLocation, float SearchRange)
{
	float SearchDistSq = FMath::Square(SearchRange); // Квадрат дистанции для оптимизации
	UHandSocketComponent* ClosestHandSocket = nullptr; // Ближайший найденный сокет
	float LastDist = 0.0f; // Последняя сохраненная дистанция (квадрат)
	float DistSq = 0.0f; // Текущая дистанция (квадрат)

	bool bFoundOne = false; // Флаг, найден ли хотя бы один сокет
	UHandSocketComponent* HandSocket = nullptr;
	FTransform HandSocketTrans;
	// Итерация по всем объектам типа UHandSocketComponent
	for (TObjectIterator<UHandSocketComponent> It; It; ++It)
	{
		HandSocket = *It;
		// Проверка на валидность и не является ли шаблоном
		if (IsValid(HandSocket) && !HandSocket->IsTemplate())
		{
			// Получаем мировую трансформацию сокета
			HandSocketTrans = HandSocket->GetRelativeTransform() * HandSocket->GetOwner()->GetActorTransform();
			DistSq = FVector::DistSquared(HandSocketTrans.GetLocation(), SearchFromWorldLocation);
			// Вычисляем квадрат дистанции
			// Если сокет в пределах дистанции И (это первый найденный ИЛИ он ближе предыдущего)
			if (DistSq <= SearchDistSq && (!bFoundOne || DistSq < LastDist))
			{
				bFoundOne = true;
				ClosestHandSocket = HandSocket; // Сохраняем как ближайший
				LastDist = DistSq; // Обновляем последнюю дистанцию
			}
		}
	}

	return ClosestHandSocket; // Возвращаем ближайший найденный сокет (может быть nullptr)
}

bool UHandSocketComponent::GetAnimationSequenceAsPoseSnapShot(UAnimSequence* InAnimationSequence,
                                                              FPoseSnapshot& OutPoseSnapShot,
                                                              USkeletalMeshComponent* TargetMesh, bool bSkipRootBone,
                                                              bool bFlipHand)
{
	if (InAnimationSequence) // Если анимация валидна
	{
		// Устанавливаем имена для снимка позы
		OutPoseSnapShot.SkeletalMeshName =
			/*TargetMesh ? TargetMesh->SkeletalMesh->GetFName(): */ InAnimationSequence->GetSkeleton()->GetFName();
		// Имя скелета
		OutPoseSnapShot.SnapshotName = InAnimationSequence->GetFName(); // Имя анимации
		OutPoseSnapShot.BoneNames.Empty(); // Очищаем массивы
		OutPoseSnapShot.LocalTransforms.Empty();

		TArray<FName> AnimSeqNames; // Не используется?

		if (USkeleton* AnimationSkele = InAnimationSequence->GetSkeleton()) // Получаем скелет из анимации
		{
			// pre-size the array to avoid unnecessary reallocation
			// предварительно выделяем память в массиве, чтобы избежать ненужных перераспределений
			OutPoseSnapShot.BoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			// Заполняем массив имен костей из референсного скелета
			for (int32 i = 0; i < AnimationSkele->GetReferenceSkeleton().GetNum(); i++)
			{
				OutPoseSnapShot.BoneNames[i] = AnimationSkele->GetReferenceSkeleton().GetBoneName(i);
				// Если нужно отзеркалить, меняем _r на _l и наоборот в именах костей
				if (bFlipHand)
				{
					FString bName = OutPoseSnapShot.BoneNames[i].ToString();

					if (bName.Contains("_r"))
					{
						bName = bName.Replace(TEXT("_r"), TEXT("_l"));
					}
					else
					{
						bName = bName.Replace(TEXT("_l"), TEXT("_r"));
					}

					OutPoseSnapShot.BoneNames[i] = FName(bName);
				}
			}
		}
		else // Если скелет не найден
		{
			return false; // Ошибка
		}

		// Получаем референсный скелет (из целевого меша, если он есть, иначе из анимации)
		const FReferenceSkeleton& RefSkeleton =
			(TargetMesh)
				? TargetMesh->GetSkinnedAsset()->GetRefSkeleton()
				: InAnimationSequence->GetSkeleton()->GetReferenceSkeleton();
		FTransform LocalTransform; // Локальная трансформация кости

		// Получаем таблицу соответствия треков анимации костям скелета
		const TArray<FTrackToSkeletonMap>& TrackMap = InAnimationSequence->GetCompressedTrackToSkeletonMapTable();
		int32 TrackIndex = INDEX_NONE; // Индекс трека анимации

		OutPoseSnapShot.LocalTransforms.Reserve(OutPoseSnapShot.BoneNames.Num()); // Резервируем место

		// Итерируем по всем костям в снимке позы
		for (int32 BoneNameIndex = 0; BoneNameIndex < OutPoseSnapShot.BoneNames.Num(); ++BoneNameIndex)
		{
			const FName& BoneName = OutPoseSnapShot.BoneNames[BoneNameIndex]; // Получаем имя кости

			// Ищем индекс трека анимации для текущей кости
			TrackIndex = INDEX_NONE;
			if (BoneNameIndex != INDEX_NONE && BoneNameIndex < TrackMap.Num() && TrackMap[BoneNameIndex].BoneTreeIndex
				== BoneNameIndex)
			{
				TrackIndex = BoneNameIndex; // Оптимизированный поиск, если индексы совпадают
			}
			else
			{
				// This shouldn't happen but I need a fallback
				// Don't currently want to reconstruct the map inversely
				// Этого не должно происходить, но нужен запасной вариант
				// В данный момент не хочу перестраивать карту в обратном порядке
				for (int i = 0; i < TrackMap.Num(); ++i)
				{
					if (TrackMap[i].BoneTreeIndex == BoneNameIndex)
					{
						TrackIndex = i;
						break;
					}
				}
			}

			// Если трек найден И (не пропускаем корневую кость ИЛИ это не корневая кость)
			if (TrackIndex != INDEX_NONE && (!bSkipRootBone || TrackIndex != 0))
			{
				double TrackLocation = 0.0f; // Время на треке (здесь используется 0.0)
				// Получаем трансформацию кости из анимации
				InAnimationSequence->GetBoneTransform(
					LocalTransform, FSkeletonPoseBoneIndex(TrackMap[TrackIndex].BoneTreeIndex), TrackLocation, false);
			}
			else // Если трек не найден или пропускаем корневую кость
			{
				// otherwise, get ref pose if exists
				// иначе, получаем референсную позу, если существует
				const int32 BoneIDX = RefSkeleton.FindBoneIndex(BoneName);
				if (BoneIDX != INDEX_NONE)
				{
					LocalTransform = RefSkeleton.GetRefBonePose()[BoneIDX]; // Берем из референсной позы
				}
				else // Если кость не найдена в референсной позе
				{
					LocalTransform = FTransform::Identity; // Используем единичную трансформацию
				}
			}

			// Если нужно отзеркалить И (не пропускаем корневую кость ИЛИ это не корневая кость)
			if (bFlipHand && (!bSkipRootBone || TrackIndex != 0))
			{
				// Отзеркаливаем трансформацию
				FMatrix M = LocalTransform.ToMatrixWithScale();
				M.Mirror(EAxis::X, EAxis::X);
				M.Mirror(EAxis::Y, EAxis::Y);
				M.Mirror(EAxis::Z, EAxis::Z);
				LocalTransform.SetFromMatrix(M);
			}

			// Добавляем локальную трансформацию в снимок позы
			OutPoseSnapShot.LocalTransforms.Add(LocalTransform);
		}

		OutPoseSnapShot.bIsValid = true; // Снимок позы валиден
		return true; // Успех
	}

	return false; // Анимация не валидна
}

bool UHandSocketComponent::GetBlendedPoseSnapShot(
	FPoseSnapshot& PoseSnapShot, USkeletalMeshComponent* TargetMesh, bool bSkipRootBone, bool bFlipHand)
{
	// Если есть целевая анимация
	if (HandTargetAnimation)
	// && bUseCustomPoseDeltas && CustomPoseDeltas.Num() > 0) // Условие на дельты закомментировано
	{
		// Устанавливаем имена для снимка позы
		PoseSnapShot.SkeletalMeshName = HandTargetAnimation->GetSkeleton()->GetFName();
		PoseSnapShot.SnapshotName = HandTargetAnimation->GetFName();
		PoseSnapShot.BoneNames.Empty(); // Очищаем массивы
		PoseSnapShot.LocalTransforms.Empty();
		TArray<FName> OrigBoneNames; // Массив для хранения оригинальных имен костей (до отзеркаливания)

		if (USkeleton* AnimationSkele = HandTargetAnimation->GetSkeleton()) // Получаем скелет
		{
			// pre-size the array to avoid unnecessary reallocation
			// предварительно выделяем память
			PoseSnapShot.BoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			OrigBoneNames.AddUninitialized(AnimationSkele->GetReferenceSkeleton().GetNum());
			// Заполняем имена костей, отзеркаливаем при необходимости
			for (int32 i = 0; i < AnimationSkele->GetReferenceSkeleton().GetNum(); i++)
			{
				PoseSnapShot.BoneNames[i] = AnimationSkele->GetReferenceSkeleton().GetBoneName(i);
				OrigBoneNames[i] = PoseSnapShot.BoneNames[i]; // Сохраняем оригинальное имя
				if (bFlipHand)
				{
					FString bName = PoseSnapShot.BoneNames[i].ToString();
					if (bName.Contains("_r"))
					{
						bName = bName.Replace(TEXT("_r"), TEXT("_l"));
					}
					else
					{
						bName = bName.Replace(TEXT("_l"), TEXT("_r"));
					}
					PoseSnapShot.BoneNames[i] = FName(bName);
				}
			}
		}
		else
		{
			return false; // Скелет не найден
		}

		// Получаем референсный скелет
		const FReferenceSkeleton& RefSkeleton =
			(TargetMesh)
				? TargetMesh->GetSkinnedAsset()->GetRefSkeleton()
				: HandTargetAnimation->GetSkeleton()->GetReferenceSkeleton();
		FTransform LocalTransform; // Локальная трансформация

		// Получаем таблицу соответствия треков костям
		const TArray<FTrackToSkeletonMap>& TrackMap = HandTargetAnimation->GetCompressedTrackToSkeletonMapTable();
		int32 TrackIndex = INDEX_NONE; // Индекс трека

		// Итерируем по всем костям
		for (int32 BoneNameIndex = 0; BoneNameIndex < PoseSnapShot.BoneNames.Num(); ++BoneNameIndex)
		{
			// Ищем индекс трека анимации
			TrackIndex = INDEX_NONE;
			if (BoneNameIndex < TrackMap.Num() && TrackMap[BoneNameIndex].BoneTreeIndex == BoneNameIndex)
			{
				TrackIndex = BoneNameIndex;
			}
			else
			{
				// This shouldn't happen but I need a fallback
				// Don't currently want to reconstruct the map inversely
				// Этого не должно происходить, но нужен запасной вариант
				// В данный момент не хочу перестраивать карту в обратном порядке
				for (int i = 0; i < TrackMap.Num(); ++i)
				{
					if (TrackMap[i].BoneTreeIndex == BoneNameIndex)
					{
						TrackIndex = i;
						break;
					}
				}
			}

			const FName& BoneName = PoseSnapShot.BoneNames[BoneNameIndex]; // Имя кости

			// Если трек найден И (не пропускаем корень ИЛИ это не корень)
			if (TrackIndex != INDEX_NONE && (!bSkipRootBone || TrackIndex != 0))
			{
				double TrackLocation = 0.0f; // Время 0.0
				// Получаем трансформацию из анимации
				HandTargetAnimation->GetBoneTransform(
					LocalTransform, FSkeletonPoseBoneIndex(TrackMap[TrackIndex].BoneTreeIndex), TrackLocation, false);
			}
			else // Иначе берем из референсной позы
			{
				// otherwise, get ref pose if exists
				// иначе, получаем референсную позу, если существует
				const int32 BoneIDX = RefSkeleton.FindBoneIndex(BoneName);
				if (BoneIDX != INDEX_NONE)
				{
					LocalTransform = RefSkeleton.GetRefBonePose()[BoneIDX];
				}
				else
				{
					LocalTransform = FTransform::Identity;
				}
			}

			// Если используем пользовательские дельты
			if (bUseCustomPoseDeltas)
			{
				FQuat DeltaQuat = FQuat::Identity;
				// Ищем дельту для оригинального имени кости
				if (FBPVRHandPoseBonePair* HandPair = CustomPoseDeltas.FindByKey(OrigBoneNames[BoneNameIndex]))
				{
					DeltaQuat = HandPair->DeltaPose; // Найдена дельта
				}

				// Применяем дельта-вращение
				LocalTransform.ConcatenateRotation(DeltaQuat);
				LocalTransform.NormalizeRotation(); // Нормализуем кватернион
			}

			// Если нужно отзеркалить И (не пропускаем корень ИЛИ это не корень)
			if (bFlipHand && (!bSkipRootBone || TrackIndex != 0))
			{
				// Отзеркаливаем трансформацию
				FMatrix M = LocalTransform.ToMatrixWithScale();
				M.Mirror(EAxis::X, EAxis::X);
				M.Mirror(EAxis::Y, EAxis::Y);
				M.Mirror(EAxis::Z, EAxis::Z);
				LocalTransform.SetFromMatrix(M);
			}

			// Добавляем финальную трансформацию в снимок
			PoseSnapShot.LocalTransforms.Add(LocalTransform);
		}

		PoseSnapShot.bIsValid = true; // Снимок валиден
		return true; // Успех
	}
	// Если нет анимации, но есть пользовательские дельты и целевой меш
	else if (bUseCustomPoseDeltas && CustomPoseDeltas.Num() && TargetMesh)
	{
		// Устанавливаем имена для снимка
		PoseSnapShot.SkeletalMeshName = TargetMesh->GetSkinnedAsset()->GetSkeleton()->GetFName();
		PoseSnapShot.SnapshotName = FName(TEXT("RawDeltaPose")); // Имя указывает, что это только дельты
		PoseSnapShot.BoneNames.Empty(); // Очищаем массивы
		PoseSnapShot.LocalTransforms.Empty();
		TargetMesh->GetBoneNames(PoseSnapShot.BoneNames); // Получаем имена костей из меша

		// PoseSnapShot.LocalTransforms = TargetMesh->GetSkinnedAsset()->GetSkeleton()->GetRefLocalPoses(); // Используем референсную позу
		// скелета
		PoseSnapShot.LocalTransforms =
			TargetMesh->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose(); // Используем референсную позу скелета

		FQuat DeltaQuat = FQuat::Identity;
		FName TargetBoneName = NAME_None; // Имя кости для поиска дельты (может быть отзеркалено)

		// Итерируем по пользовательским дельтам
		for (FBPVRHandPoseBonePair& HandPair : CustomPoseDeltas)
		{
			// Определяем целевое имя кости (отзеркаливаем, если нужно)
			if (bFlipHand)
			{
				FString bName = HandPair.BoneName.ToString();
				if (bName.Contains("_r"))
				{
					bName = bName.Replace(TEXT("_r"), TEXT("_l"));
				}
				else
				{
					bName = bName.Replace(TEXT("_l"), TEXT("_r"));
				}
				TargetBoneName = FName(bName);
			}
			else
			{
				TargetBoneName = HandPair.BoneName;
			}

			// Находим индекс кости в меше
			int32 BoneIdx = TargetMesh->GetBoneIndex(TargetBoneName);
			if (BoneIdx != INDEX_NONE) // Если кость найдена
			{
				DeltaQuat = HandPair.DeltaPose; // Берем дельту

				// Если нужно отзеркалить, отзеркаливаем дельту
				if (bFlipHand)
				{
					FTransform DeltaTrans(DeltaQuat);
					FMatrix M = DeltaTrans.ToMatrixWithScale();
					M.Mirror(EAxis::X, EAxis::X);
					M.Mirror(EAxis::Y, EAxis::Y);
					M.Mirror(EAxis::Z, EAxis::Z);
					DeltaTrans.SetFromMatrix(M);
					DeltaQuat = DeltaTrans.GetRotation();
				}

				// Применяем дельта-вращение к референсной позе
				PoseSnapShot.LocalTransforms[BoneIdx].ConcatenateRotation(DeltaQuat);
				PoseSnapShot.LocalTransforms[BoneIdx].NormalizeRotation(); // Нормализуем
			}
		}

		PoseSnapShot.bIsValid = true; // Снимок валиден
		return true; // Успех
	}

	return false; // Не удалось создать снимок
}

FTransform UHandSocketComponent::GetHandRelativePlacement()
{
	// Optionally mirror for left hand // Опционально отзеркалить для левой руки (Не отзеркаливается здесь)

	// Если размещение меша отсоединено
	if (bDecoupleMeshPlacement)
	{
		if (USceneComponent* ParentComp = GetAttachParent()) // Если есть родитель
		{
			// Возвращаем относительную трансформацию HandRelativePlacement относительно текущей относительной трансформации компонента
			return HandRelativePlacement.GetRelativeTransform(this->GetRelativeTransform());
			// FTransform curTrans = HandRelativePlacement * ParentComp->GetComponentTransform(); // Закомментированный альтернативный
			// расчет return curTrans.GetRelativeTransform(this->GetComponentTransform()); // Закомментированный альтернативный расчет
		}
	}

	return HandRelativePlacement; // Возвращаем базовое значение
}

FTransform UHandSocketComponent::GetHandSocketTransform(UGripMotionControllerComponent* QueryController,
                                                        bool bIgnoreOnlySnapMesh)
{
	// Optionally mirror for left hand // Опционально отзеркалить для левой руки

	// Если сокет используется только для привязки меша и мы это не игнорируем
	if (!bIgnoreOnlySnapMesh && bOnlySnapMesh)
	{
		if (!QueryController) // Если контроллер не предоставлен (обязателен для этого режима)
		{
			// No controller input // Нет ввода контроллера
			UE_LOG(LogVRMotionController, Warning,
			       TEXT(
				       "HandSocketComponent::GetHandSocketTransform was missing required motion controller for bOnlySnapMesh! Check that you "
				       "are passing a controller into GetClosestSocketInRange!"));
		}
		else // Если контроллер есть
		{
			// Возвращаем трансформацию пивота контроллера (рука следует за контроллером)
			return QueryController->GetPivotTransform();
		}
	}

	// Если нужно отзеркалить для другой руки
	if (bFlipForLeftHand)
	{
		if (!QueryController) // Если контроллер не предоставлен (обязателен для определения руки)
		{
			// No controller input // Нет ввода контроллера
			UE_LOG(LogVRMotionController, Warning,
			       TEXT(
				       "HandSocketComponent::GetHandSocketTransform was missing required motion controller for bFlipForLeftand! Check that "
				       "you are passing a controller into GetClosestSocketInRange!"));
		}
		else // Если контроллер есть
		{
			EControllerHand HandType;
			QueryController->GetHandType(HandType); // Получаем тип руки
			bool bIsRightHand = HandType == EControllerHand::Right; // Это правая рука?
			// Если доминантность сокета совпадает с "правой рукой" (т.е. левая доминантность и правая рука, или правая доминантность и
			// левая рука)
			if (bLeftHandDominant == bIsRightHand)
			{
				FTransform ReturnTrans = this->GetRelativeTransform(); // Берем относительную трансформацию
				if (USceneComponent* AttParent = this->GetAttachParent()) // Если есть родитель
				{
					// Отзеркаливаем относительную трансформацию
					ReturnTrans.Mirror(GetAsEAxis(MirrorAxis), GetAsEAxis(FlipAxis));
					// Если отзеркаливаем только вращение, восстанавливаем исходное положение
					if (bOnlyFlipRotation)
					{
						ReturnTrans.SetTranslation(this->GetRelativeLocation());
					}

					// Если прикреплены к сокету родителя, умножаем на трансформацию сокета
					if (this->GetAttachSocketName() != NAME_None)
					{
						ReturnTrans = ReturnTrans * AttParent->GetSocketTransform(GetAttachSocketName(), RTS_Component);
					}

					// Преобразуем в мировое пространство
					ReturnTrans = ReturnTrans * AttParent->GetComponentTransform();
				}

				return ReturnTrans; // Возвращаем отзеркаленную мировую трансформацию
			}
		}
	}

	// Если не отзеркаливали или не нужно было

	// Если сокет заблокирован на месте
	if (bLockInPlace)
	{
		FTransform ReturnTrans = this->GetRelativeTransform(); // Берем относительную трансформацию

		if (USceneComponent* AttParent = this->GetAttachParent()) // Если есть родитель
		{
			// Если прикреплены к сокету, учитываем его трансформацию
			if (this->GetAttachSocketName() != NAME_None)
			{
				ReturnTrans = ReturnTrans * AttParent->GetSocketTransform(GetAttachSocketName(), RTS_Component);
			}

			// Преобразуем в мировое пространство
			ReturnTrans = ReturnTrans * AttParent->GetComponentTransform();
		}
		else // Если родителя нет (маловероятно)
		{
			ReturnTrans = this->GetComponentTransform(); // Fallback // Запасной вариант: текущая мировая трансформация
		}

		return ReturnTrans; // Возвращаем вычисленную мировую трансформацию
	}
	else // Если сокет не заблокирован
	{
		// Просто возвращаем текущую мировую трансформацию компонента
		return this->GetComponentTransform();
	}
}

FTransform UHandSocketComponent::GetMeshRelativeTransform(bool bIsRightHand, bool bUseParentScale, bool bUseMirrorScale)
{
	// Optionally mirror for left hand // Опционально отзеркалить для левой руки

	// Failsafe // Защита от сбоя
	if (!this->GetAttachParent()) return FTransform::Identity; // Возвращаем единичную трансформацию, если нет родителя

	FTransform relTrans = this->GetRelativeTransform(); // Относительная трансформация сокета к родителю
	FTransform HandTrans = GetHandRelativePlacement(); // Относительная трансформация руки к сокету
	// <-- НОВОЕ: приводим авторский фрейм слота к hand-фрейму кости
	HandTrans.ConcatenateRotation(AuthoringToHandRotationOffset.Quaternion());
	FTransform ReturnTrans = FTransform::Identity; // Результирующая трансформация

	{
		static bool bOnceGS = false;
		if (!bOnceGS)
		{
			bOnceGS = true;
			UE_LOG(LogTemp, Warning,
			       TEXT(
				       "[GetMeshRelativeTransform|PRE] bIsRightHand=%s  bUseParentScale=%s  bUseMirrorScale=%s  AttachSocket=%s"
			       ),
			       bIsRightHand ? TEXT("TRUE") : TEXT("FALSE"),
			       bUseParentScale ? TEXT("TRUE") : TEXT("FALSE"),
			       bUseMirrorScale ? TEXT("TRUE") : TEXT("FALSE"),
			       *GetAttachSocketName().ToString()
			);

			UE_LOG(LogTemp, Warning,
			       TEXT("[GetMeshRelativeTransform|PRE] relTrans(Socket->Parent)=%s  HandTrans(Hand->Socket)=%s"),
			       *relTrans.ToString(),
			       *HandTrans.ToString()
			);
		}
	}

	// Fix the scale // Корректируем масштаб
	// Если не используем масштаб родителя И сокет использует абсолютный масштаб
	if (!bUseParentScale && this->IsUsingAbsoluteScale() /*&& !bDecoupleMeshPlacement*/) // Закомментированное условие
	{
		FVector ParentScale = this->GetAttachParent()->GetComponentScale(); // Получаем масштаб родителя
		// Take parent scale out of our relative transform early
		// Убираем масштаб родителя из нашей относительной трансформации заранее
		relTrans.ScaleTranslation(ParentScale);
		// Вычисляем финальную трансформацию относительно родителя БЕЗ учета его масштаба
		ReturnTrans = HandTrans * relTrans;
		// We add in the inverse of the parent scale to adjust the hand mesh
		// Мы добавляем обратный масштаб родителя, чтобы скорректировать меш руки
		ReturnTrans.ScaleTranslation((FVector(1.0f) / ParentScale));
		ReturnTrans.SetScale3D(FVector(1.0f)); // Устанавливаем масштаб в 1.0
	}
	else // Иначе просто комбинируем трансформации
	{
		ReturnTrans = HandTrans * relTrans;
	}

	// If we should mirror the transform, do it now that it is in our parent relative space
	// Если нам нужно отзеркалить трансформацию, делаем это сейчас, пока она в относительном пространстве родителя
	if ((bFlipForLeftHand && (bLeftHandDominant == bIsRightHand))) // Если нужно отзеркалить для текущей руки
	{
		// FTransform relTrans = this->GetRelativeTransform(); // Закомментировано, уже получено выше
		MirrorHandTransform(ReturnTrans, relTrans); // Отзеркаливаем результирующую трансформацию

		{
			static bool bOnceMirror = false;
			if (!bOnceMirror)
			{
				bOnceMirror = true;
				UE_LOG(LogTemp, Warning,
				       TEXT(
					       "[GetMeshRelativeTransform|MIRROR APPLIED] ReturnTrans=%s  MirroredScale=%s  bLeftHandDominant=%s  bFlipForLeftHand=%s"
				       ),
				       *ReturnTrans.ToString(),
				       *MirroredScale.ToString(),
				       bLeftHandDominant ? TEXT("TRUE") : TEXT("FALSE"),
				       bFlipForLeftHand ? TEXT("TRUE") : TEXT("FALSE")
				);
			}
		}

		// Если нужно использовать отзеркаленный масштаб
		if (bUseMirrorScale)
		{
			// Применяем знаковый вектор отзеркаленного масштаба к текущему масштабу
			ReturnTrans.SetScale3D(ReturnTrans.GetScale3D() * MirroredScale.GetSignVector());
		}
	}

	// Если игнорируем кость прикрепления и прикреплены к сокету
	if (bIgnoreAttachBone && this->GetAttachSocketName() != NAME_None)
	{
		// Домножаем на трансформацию сокета родителя (чтобы получить финальную трансформацию относительно родителя)
		ReturnTrans = ReturnTrans * GetAttachParent()->GetSocketTransform(GetAttachSocketName(), RTS_Component);
	}

	// ---- [DEBUG:GetMeshRelativeTransform] BEGIN ----
	UE_LOG(LogVRHandSocketComponent, Warning,
	       TEXT("[GetMeshRelativeTransform] ReturnTrans(RelToParent)=%s"), *ReturnTrans.ToString());
	// ---- [DEBUG:GetMeshRelativeTransform] END ----

	return ReturnTrans; // Возвращаем финальную относительную трансформацию меша к родителю сокета
}

#if WITH_EDITORONLY_DATA // Улучшенная функция, теперь ищем по названиям костей, а не только по индексу
FTransform UHandSocketComponent::GetBoneTransformAtTime(
	UAnimSequence* MyAnimSequence, /*float AnimTime,*/ int BoneIdx, FName BoneName, bool bUseRawDataOnly)
{
	// Безопасные проверки как и раньше
	if (!MyAnimSequence)
		return FTransform::Identity;
	if (!MyAnimSequence->GetSkeleton())
		return FTransform::Identity;

	const double tracklen = MyAnimSequence->GetPlayLength(); // как и было — берём конец клипа
	FTransform BoneTransform = FTransform::Identity;

	IAnimationDataController& AnimController = MyAnimSequence->GetController();
	const IAnimationDataModel* AnimModel = AnimController.GetModel();
	if (!AnimModel)
		return FTransform::Identity;

	const TArray<FTrackToSkeletonMap>& TrackMap = MyAnimSequence->GetCompressedTrackToSkeletonMapTable();
	if (TrackMap.Num() == 0)
		return FTransform::Identity;

	// -----------------------------
	// 1) Пытаемся найти трек по ИМЕНИ кости (надёжно для full-body)
	// -----------------------------
	int32 TrackIndex = INDEX_NONE;

	if (BoneName != NAME_None)
	{
		const USkeleton* SeqSkeleton = MyAnimSequence->GetSkeleton();
		const FReferenceSkeleton& RefSkel = SeqSkeleton->GetReferenceSkeleton();

		// Индекс кости в дереве скелета анимации
		const int32 AnimBoneIdx = RefSkel.FindBoneIndex(BoneName);
		if (AnimBoneIdx != INDEX_NONE)
		{
			// Находим соответствующий трек
			for (int32 i = 0; i < TrackMap.Num(); ++i)
			{
				if (TrackMap[i].BoneTreeIndex == AnimBoneIdx)
				{
					TrackIndex = i;
					break;
				}
			}
		}
	}

	// -----------------------------
	// 2) Если по имени не нашли — сохраняем вашу оригинальную «индексную» логику (совместимость)
	// -----------------------------
	if (TrackIndex == INDEX_NONE)
	{
		if (BoneIdx != INDEX_NONE && BoneIdx < TrackMap.Num() && TrackMap[BoneIdx].BoneTreeIndex == BoneIdx)
		{
			TrackIndex = BoneIdx;
		}
		else
		{
			// Fallback-поиск по значению BoneIdx в таблице
			for (int32 i = 0; i < TrackMap.Num(); ++i)
			{
				if (TrackMap[i].BoneTreeIndex == BoneIdx)
				{
					TrackIndex = i;
					break;
				}
			}
		}
	}

	// -----------------------------
	// 3) Возвращаем трансформ, если всё нашлось
	// -----------------------------
	if (TrackIndex == INDEX_NONE)
		return FTransform::Identity;

	const FSkeletonPoseBoneIndex PoseBoneIndex(TrackMap[TrackIndex].BoneTreeIndex);
	if (!PoseBoneIndex.IsValid())
		return FTransform::Identity;

	MyAnimSequence->GetBoneTransform(BoneTransform, PoseBoneIndex, /*AnimTime*/ tracklen, bUseRawDataOnly);
	return BoneTransform;
}
#endif

void UHandSocketComponent::OnRegister()
{
	UWorld* MyWorld = GetWorld();
	if (!MyWorld) return;

	TEnumAsByte<EWorldType::Type> MyWorldType = MyWorld->WorldType; // Получаем тип мира

#if WITH_EDITORONLY_DATA
	AActor* MyOwner = GetOwner();

	// --- ПОКАЗ ВИЗУАЛИЗАТОРА ---
	if (bShowVisualizationMesh && MyOwner && !IsRunningCommandlet() &&
		(MyWorldType == EWorldType::Editor || MyWorldType == EWorldType::EditorPreview))
	{
		// [ДОБАВЛЕНО] 0) Защитная очистка: на случай, если в акторе уже есть "осиротевшие"
		// визуализаторы от этого сокета, уничтожим их перед созданием нового.
		{
			TArray<UPoseableMeshComponent*> PoseableComponents;
			MyOwner->GetComponents(PoseableComponents);
			for (UPoseableMeshComponent* ComponentToClean : PoseableComponents)
			{
				if (ComponentToClean && ComponentToClean->IsVisualizationComponent() && ComponentToClean->
					GetAttachParent() == this && ComponentToClean != HandVisualizerComponent)
				{
					ComponentToClean->DestroyComponent();
				}
			}
		}

		// 1) Создание при необходимости (НЕ удаляем существующий)
		if (!HandVisualizerComponent)
		{
			// [ИЗМЕНЕНО] Создаём компонент, где 'Outer' - это сам сокет (this), а не его владелец.
			// Это привязывает жизненный цикл визуализатора к сокету.
			// Убираем флаг RF_Transactional и заменяем его на RF_Transient, чтобы система Undo/Redo
			// не отслеживала этот временный компонент.
			HandVisualizerComponent = NewObject<UPoseableMeshComponent>(
				this, TEXT("HandSocketVisualizer"), RF_Transient | RF_TextExportTransient);

			HandVisualizerComponent->SetupAttachment(this);
			HandVisualizerComponent->SetIsVisualizationComponent(true);
			HandVisualizerComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
			HandVisualizerComponent->CastShadow = false;
			HandVisualizerComponent->CreationMethod = CreationMethod;
			HandVisualizerComponent->SetComponentTickEnabled(false);
			HandVisualizerComponent->SetHiddenInGame(true);
			HandVisualizerComponent->RegisterComponentWithWorld(MyWorld);
			HandVisualizerComponent->SetVisibility(true, true); // явно включим видимость в редакторе
		}

		if (HandVisualizerComponent)
		{
			bTickedPose = false;

			// 2) Назначаем меш и материалы (только если есть что назначать)
			if (VisualizationMesh)
			{
				// Обновляем только если меш поменялся
				if (HandVisualizerComponent->GetSkinnedAsset() != VisualizationMesh)
				{
					HandVisualizerComponent->SetSkinnedAssetAndUpdate(VisualizationMesh);
				}

				if (HandPreviewMaterial)
				{
					// Теперь этот вызов вернет правильное количество материалов (в вашем случае, 2).
					const int32 NumMaterials = HandVisualizerComponent->GetNumMaterials();
					// И наш цикл корректно применит материал ко всем слотам.
					for (int32 MaterialIndex = 0; MaterialIndex < NumMaterials; ++MaterialIndex)
					{
						HandVisualizerComponent->SetMaterial(MaterialIndex, (UMaterialInterface*)HandPreviewMaterial);
					}
				}
			}

			// 3) СНАЧАЛА применяем позу, ПОТОМ позиционируем (важный порядок!)
			PoseVisualizationToAnimation(true);

			if (HandRootBoneNameForPose != NAME_None)
				PositionFullBodyVisualizationMesh();
			else
				PositionVisualizationMesh();
		}
	}
	// --- СКРЫТИЕ/УДАЛЕНИЕ ВИЗУАЛИЗАТОРА ---
	else if (HandVisualizerComponent)
	{
		// Вне редактора — уничтожаем без условий, иначе возможна утечка
		HandVisualizerComponent->DestroyComponent();
		HandVisualizerComponent = nullptr;
	}
#endif // WITH_EDITORONLY_DATA

	// Блокировка только в игре/PIE
	if (bLockInPlace && (MyWorldType != EWorldType::Editor && MyWorldType != EWorldType::EditorPreview))
	{
		SetUsingAbsoluteLocation(true);
		SetUsingAbsoluteRotation(true);
	}

	Super::OnRegister();
}


// [НАЧАЛО ДОБАВЛЕННОГО БЛОКА]

void UHandSocketComponent::OnUnregister()
{
#if WITH_EDITORONLY_DATA
	// Гарантированно уничтожаем визуализатор при снятии компонента с регистрации,
	// чтобы предотвратить появление "зомби-компонентов" в редакторе.
	if (HandVisualizerComponent)
	{
		HandVisualizerComponent->DestroyComponent();
		HandVisualizerComponent = nullptr;
	}

#endif

	Super::OnUnregister();
}

#if WITH_EDITORONLY_DATA
void UHandSocketComponent::PositionVisualizationMesh()
{
	if (!HandVisualizerComponent) // Если нет компонента визуализации, выходим
	{
		return;
	}

	if (USceneComponent* ParentAttach = this->GetAttachParent()) // Если есть родитель
	{
		FTransform relTrans = this->GetRelativeTransform(); // Относительная трансформация сокета

		// Если состояние отсоединенности изменилось в редакторе
		if (bDecoupled != bDecoupleMeshPlacement)
		{
			// Корректируем HandRelativePlacement, чтобы сохранить мировую позицию руки
			if (bDecoupleMeshPlacement)
			{
				HandRelativePlacement = HandRelativePlacement * GetRelativeTransform();
			}
			else
			{
				HandRelativePlacement = HandRelativePlacement.GetRelativeTransform(GetRelativeTransform());
			}
		}

		FTransform HandPlacement = GetHandRelativePlacement(); // Получаем относительное размещение руки к сокету
		// Вычисляем трансформацию руки относительно родителя сокета
		FTransform ReturnTrans = (HandPlacement * relTrans);

		// Если нужно отзеркалить визуализацию
		if (bMirrorVisualizationMesh) //(bFlipForLeftHand && !bIsRightHand)) // Закомментированное старое условие
		{
			MirrorHandTransform(ReturnTrans, relTrans); // Отзеркаливаем
		}

		// Применяем отзеркаленный масштаб, если это левая доминантная рука без отзеркаливания, или правая с отзеркаливанием
		if ((bLeftHandDominant && !bMirrorVisualizationMesh) || (!bLeftHandDominant && bMirrorVisualizationMesh))
		{
			ReturnTrans.SetScale3D(ReturnTrans.GetScale3D() * MirroredScale);
		}

		// Устанавливаем относительную трансформацию компонента визуализации
		HandVisualizerComponent->SetRelativeTransform(
			ReturnTrans.GetRelativeTransform(relTrans) /*newRel*/); // Закомментированная переменная
	}
}

void UHandSocketComponent::HideVisualizationMesh()
{
	// Если визуализацию показывать не нужно и компонент существует
	if (!bShowVisualizationMesh && HandVisualizerComponent)
	{
		// Скрываем и уничтожаем
		HandVisualizerComponent->SetVisibility(false);
		HandVisualizerComponent->DestroyComponent();
		HandVisualizerComponent = nullptr;
	}
}

#endif

#if WITH_EDITORONLY_DATA
void UHandSocketComponent::PositionFullBodyVisualizationMesh()
{
	// Проверки: сам визуализатор, скелет и скин обязаны быть
	if (!HandVisualizerComponent || !VisualizationMesh || !HandVisualizerComponent->GetSkinnedAsset())
	{
		return;
	}

	if (USceneComponent* ParentAttach = GetAttachParent())
	{
		const FTransform relTrans = this->GetRelativeTransform();

		if (bDecoupled != bDecoupleMeshPlacement)
		{
			if (bDecoupleMeshPlacement)
			{
				HandRelativePlacement = HandRelativePlacement * GetRelativeTransform();
			}
			else
			{
				HandRelativePlacement = HandRelativePlacement.GetRelativeTransform(GetRelativeTransform());
			}
		}

		// --- РЕФАКТОРИНГ: Используем GetMeshRelativeTransform для единой логики ---
		// Определяем "руку" для предпросмотра на основе флагов доминантности и отзеркаливания.
		// Это гарантирует, что мы вызываем GetMeshRelativeTransform с теми же параметрами,
		// которые будут использоваться для определения отзеркаливания в рантайме.
		const bool bIsRightHandForPreview = bLeftHandDominant ? bMirrorVisualizationMesh : !bMirrorVisualizationMesh;

		// Получаем базовую "игровую" трансформацию. Теперь она учитывает все флаги
		// (bIgnoreAttachBone, абсолютный скейл и т.д.) так же, как и в рантайме.
		// Мы используем bUseMirrorScale = true для визуализатора, чтобы видеть корректный отзеркаленный масштаб.
		FTransform ReturnTrans = GetMeshRelativeTransform(bIsRightHandForPreview, false, /*bUseMirrorScale*/true);

		// --- НОВОЕ: Динамический офсет до кисти для WYSIWYG ---
		FTransform InverseHandOffset = FTransform::Identity;

		if (bUseAdvancedFullBodyPreview && HandRootBoneNameForPose != NAME_None)
		{
			// К этому моменту уже должен быть вызван PoseVisualizationToAnimation(true)
			if (UPoseableMeshComponent* Poseable = HandVisualizerComponent)
			{
				// Проверяем, существует ли кость, прежде чем запрашивать ее трансформацию
				if (Poseable->GetBoneIndex(HandRootBoneNameForPose) != INDEX_NONE)
				{
					// [ИСПРАВЛЕНО] Используем GetBoneTransformByName для UE5
					const FTransform HandCS = Poseable->GetBoneTransformByName(
						HandRootBoneNameForPose, EBoneSpaces::ComponentSpace);
					InverseHandOffset = HandCS.Inverse();
				}
			}
		}
		else if (HandRootBoneNameForPose != NAME_None)
		{
			// Фоллбэк: офсет из ReferenceSkeleton (старая логика)
			const FReferenceSkeleton& RefSkeleton = VisualizationMesh->GetRefSkeleton();
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(HandRootBoneNameForPose);
			if (BoneIndex != INDEX_NONE)
			{
				// Мы не можем просто взять локальную позу кости. Нам нужно вычислить её полную
				// трансформацию относительно корневой кости всего скелета. Для этого мы
				// итерируемся вверх по иерархии родителей и перемножаем их трансформации.
				FTransform BoneFullTransform = RefSkeleton.GetRefBonePose()[BoneIndex];
				int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
				while (ParentIndex != INDEX_NONE)
				{
					BoneFullTransform = BoneFullTransform * RefSkeleton.GetRefBonePose()[ParentIndex];
					ParentIndex = RefSkeleton.GetParentIndex(ParentIndex);
				}
				InverseHandOffset = BoneFullTransform.Inverse();
			}
		}
		// --- КОНЕЦ расчета офсета ---

		// Финальный расчет позиции для визуализатора.
		HandVisualizerComponent->SetRelativeTransform(InverseHandOffset * ReturnTrans.GetRelativeTransform(relTrans));

		// ===== DEBUG (EDITOR): сравнение превью и рантайма =====
#if WITH_EDITOR
		{
			const USceneComponent* ParentAttachs = GetAttachParent();
			const FTransform ParentWorld =
				ParentAttachs ? ParentAttachs->GetComponentTransform() : FTransform::Identity;
			const FTransform CompWorld = HandVisualizerComponent->GetComponentTransform();

			// МИР кости кисти на визуализаторе (после применения позы и InverseHandOffset)
			FTransform WorldPreviewBone = CompWorld;
			if (HandRootBoneNameForPose != NAME_None)
			{
				const FTransform HandCS = HandVisualizerComponent->
					GetBoneTransformByName(HandRootBoneNameForPose, EBoneSpaces::ComponentSpace);
				WorldPreviewBone = HandCS * CompWorld;
			}

			// Что будет в игре (IK-таргет)
			const FTransform WorldRuntime = ReturnTrans * ParentWorld;

			UE_LOG(LogVRHandSocketComponent, Verbose,
			       TEXT("[EDITOR][%s] Preview placed (HandRelativePlacement=%s)"),
			       *GetNameSafe(this), *HandRelativePlacement.ToString());
		}
#endif
	}
}
#endif

#if WITH_EDITORONLY_DATA
void UHandSocketComponent::PoseVisualizationToAnimation(bool bForceRefresh)
{
	// Если нет компонента визуализации или у него нет скелетного ассета, выходим
	if (!HandVisualizerComponent || !HandVisualizerComponent->GetSkinnedAsset()) return;

	TArray<FTransform> LocalPoses; // Массив для хранения локальных поз
	// Если нет целевой анимации, берем референсную позу
	if (!HandTargetAnimation)
	{
		// Store local poses for posing // Сохранить локальные позы для позирования
		LocalPoses = HandVisualizerComponent->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose();
	}

	// Check out of the skin cache, the poses don't update otherwise when enabled
	// Отключаем кэш скининга, иначе позы не обновляются при его включении
	int32 NumLODs = HandVisualizerComponent->GetNumLODs();
	HandVisualizerComponent->SkinCacheUsage.Empty(NumLODs);
	for (int nLODs = 0; nLODs <= NumLODs; ++nLODs)
	{
		HandVisualizerComponent->SkinCacheUsage.Add(ESkinCacheUsage::Disabled);
	}

	// Now Pose the bones // Теперь позируем кости
	TArray<FName> BonesNames; // Массив имен костей
	HandVisualizerComponent->GetBoneNames(BonesNames); // Получаем имена костей
	int32 Bones = HandVisualizerComponent->GetNumBones(); // Количество костей

	// Итерируем по всем костям
	for (int32 i = 0; i < Bones; i++)
	{
		// Если нет ни анимации, ни пользовательских дельт, сбрасываем трансформацию кости
		if (!HandTargetAnimation && !bUseCustomPoseDeltas)
		{
			if (HandVisualizerComponent->GetBoneSpaceTransforms().Num() > 0) // Проверка, есть ли уже трансформации
			{
				HandVisualizerComponent->ResetBoneTransformByName(BonesNames[i]); // Сбрасываем
			}
			continue; // Переходим к следующей кости
		}

		// Получаем родительскую кость и ее трансформацию
		FName ParentBone = HandVisualizerComponent->GetParentBone(BonesNames[i]);
		FTransform ParentTrans = FTransform::Identity;
		if (ParentBone != NAME_None)
		{
			ParentTrans = HandVisualizerComponent->GetBoneTransformByName(ParentBone, EBoneSpaces::ComponentSpace);
		}

		FQuat DeltaQuat = FQuat::Identity; // Дельта-вращение по умолчанию
		// Если используем пользовательские дельты
		if (bUseCustomPoseDeltas)
		{
			// Ищем дельту для текущей кости
			for (FBPVRHandPoseBonePair BonePairC : CustomPoseDeltas)
			{
				if (BonePairC.BoneName == BonesNames[i])
				{
					DeltaQuat = BonePairC.DeltaPose; // Найдена дельта
					DeltaQuat.Normalize(); // Нормализуем
					break;
				}
			}
		}

		FTransform BoneTrans = FTransform::Identity; // Трансформация кости

		// Если есть целевая анимация
		if (HandTargetAnimation)
		{
			// Получаем трансформацию из анимации
			BoneTrans =
				GetBoneTransformAtTime(HandTargetAnimation, /*FLT_MAX,*/ i, BonesNames[i], false);
			// true; // Закомментированные параметры
		}
		else // Если нет анимации (значит, используем только дельты)
		{
			BoneTrans = LocalPoses[i]; // Берем из сохраненной референсной позы
			// BoneTrans = HandVisualizerComponent->GetSkinnedAsset()->GetRefSkeleton().GetRefBonePose()[i]; // Альтернатива
		}

		// Преобразуем в пространство компонента, умножая на трансформацию родителя
		BoneTrans =
			BoneTrans *
			ParentTrans;
		// *HandVisualizerComponent->GetComponentTransform(); // Умножение на мировую трансформацию закомментировано
		BoneTrans.NormalizeRotation(); // Нормализуем вращение

		// DeltaQuat *= HandVisualizerComponent->GetComponentTransform().GetRotation().Inverse(); // Закомментировано: применение дельты в
		// мировом пространстве?

		// Применяем дельта-вращение
		BoneTrans.ConcatenateRotation(DeltaQuat);
		BoneTrans.NormalizeRotation(); // Нормализуем снова
		// Устанавливаем финальную трансформацию кости в пространстве компонента
		HandVisualizerComponent->SetBoneTransformByName(BonesNames[i], BoneTrans, EBoneSpaces::ComponentSpace);
	}

	// Если компонент визуализации существует И (поза еще не обновлялась ИЛИ принудительное обновление)
	if (HandVisualizerComponent && (!bTickedPose || bForceRefresh))
	{
		// Tick Pose first // Сначала обновляем позу
		if (HandVisualizerComponent->IsRegistered()) // Если компонент зарегистрирован
		{
			bTickedPose = true; // Устанавливаем флаг
			HandVisualizerComponent->TickPose(1.0f, false); // Обновляем позу
			// Если есть ведущий компонент позы, обновляем ведомый
			if (HandVisualizerComponent->LeaderPoseComponent.IsValid())
			{
				HandVisualizerComponent->UpdateFollowerComponent();
			}
			else // Иначе обновляем трансформации костей напрямую
			{
				HandVisualizerComponent->RefreshBoneTransforms(&HandVisualizerComponent->PrimaryComponentTick);
			}
		}
	}
}

void UHandSocketComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UHandSocketComponent* This = CastChecked<UHandSocketComponent>(InThis);
	// Добавляем компонент визуализации в сборщик мусора
	Collector.AddReferencedObject(This->HandVisualizerComponent);

	Super::AddReferencedObjects(InThis, Collector); // Вызываем базовую функцию
}

void UHandSocketComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy); // Вызываем базовую функцию

	// Если компонент визуализации существует, уничтожаем его
	if (HandVisualizerComponent)
	{
		HandVisualizerComponent->DestroyComponent();
	}
}

#endif

void UHandSocketComponent::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); // Вызываем базовую функцию

	// For std properties // Для стандартных свойств
	FDoRepLifetimeParams PushModelParams{COND_None, REPNOTIFY_OnChanged, /*bIsPushBased=*/true};
	// Параметры репликации PushModel

	// Реплицируем флаги с использованием PushModel
	DOREPLIFETIME_WITH_PARAMS_FAST(UHandSocketComponent, bRepGameplayTags, PushModelParams);
	DOREPLIFETIME_WITH_PARAMS_FAST(UHandSocketComponent, bReplicateMovement, PushModelParams);

	// For properties with special conditions // Для свойств с особыми условиями
	FDoRepLifetimeParams PushModelParamsWithCondition{
		COND_Custom, REPNOTIFY_OnChanged, /*bIsPushBased=*/true
	}; // Параметры с условием COND_Custom

	// Реплицируем теги с условием (вероятно, проверяется bRepGameplayTags в PreReplication)
	DOREPLIFETIME_WITH_PARAMS_FAST(UHandSocketComponent, GameplayTags, PushModelParamsWithCondition);
}

void UHandSocketComponent::PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker); // Вызываем базовую функцию

	// Don't replicate if set to not do it // Не реплицировать, если установлено не делать этого
	// Активируем/деактивируем репликацию GameplayTags в зависимости от флага bRepGameplayTags
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(UHandSocketComponent, GameplayTags, bRepGameplayTags);

	// Активируем/деактивируем репликацию стандартных свойств движения SceneComponent в зависимости от флага bReplicateMovement
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeLocation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeRotation, bReplicateMovement);
	DOREPLIFETIME_ACTIVE_OVERRIDE_FAST(USceneComponent, RelativeScale3D, bReplicateMovement);
}

//=============================================================================
UHandSocketComponent::~UHandSocketComponent()
{
}

#if WITH_EDITOR
void UHandSocketComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property == nullptr) return;

	const FName ChangedPropName = PropertyChangedEvent.Property->GetFName();

#if WITH_EDITORONLY_DATA
	// Группа 1: Свойства, требующие полного пересчета позы и позиции.
	const bool bRequiresReposeAndReposition =
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandTargetAnimation) ||
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, VisualizationMesh) ||
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, CustomPoseDeltas) ||
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bUseAdvancedFullBodyPreview) ||
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRootBoneNameForPose) ||
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bMirrorVisualizationMesh) ||
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bLeftHandDominant) ||
		ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bDecoupleMeshPlacement);


	if (bRequiresReposeAndReposition)
	{
		// Всегда выполняем в правильном порядке: сначала поза, затем позиция.
		PoseVisualizationToAnimation(true);

		if (HandRootBoneNameForPose != NAME_None)
		{
			PositionFullBodyVisualizationMesh();
		}
		else
		{
			PositionVisualizationMesh();
		}
		return; // Выходим, так как обновление завершено.
	}

	// Группа 2: Свойства, требующие только пересчета позиции.
	if (ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, HandRelativePlacement))
	{
		if (HandRootBoneNameForPose != NAME_None)
		{
			PositionFullBodyVisualizationMesh();
		}
		else
		{
			PositionVisualizationMesh();
		}
		return;
	}

	// Группа 3: Свойства, управляющие видимостью.
	if (ChangedPropName == GET_MEMBER_NAME_CHECKED(UHandSocketComponent, bShowVisualizationMesh))
	{
		HideVisualizationMesh();
		return;
	}
#endif
}
#endif

UHandSocketComponent* UHandSocketComponent::GetHandSocketComponentFromObject(UObject* ObjectToCheck, FName SocketName)
{
	// Если объект является актором
	if (AActor* OwningActor = Cast<AActor>(ObjectToCheck))
	{
		// Если у актора есть корневой компонент сцены
		if (USceneComponent* OwningRoot = Cast<USceneComponent>(OwningActor->GetRootComponent()))
		{
			// Получаем дочерние компоненты
			TArray<USceneComponent*> AttachChildren = OwningRoot->GetAttachChildren();
			// Итерируем по дочерним компонентам
			for (USceneComponent* AttachChild : AttachChildren)
			{
				// Если компонент является UHandSocketComponent и имя совпадает
				if (AttachChild && AttachChild->IsA<UHandSocketComponent>() && AttachChild->GetFName() == SocketName)
				{
					return Cast<UHandSocketComponent>(AttachChild); // Возвращаем найденный компонент
				}
			}
		}
	}
	// Иначе если объект является компонентом сцены
	else if (USceneComponent* OwningRoot = Cast<USceneComponent>(ObjectToCheck))
	{
		// Получаем дочерние компоненты
		TArray<USceneComponent*> AttachChildren = OwningRoot->GetAttachChildren();
		// Итерируем по дочерним компонентам
		for (USceneComponent* AttachChild : AttachChildren)
		{
			// Если компонент является UHandSocketComponent и имя совпадает
			if (AttachChild && AttachChild->IsA<UHandSocketComponent>() && AttachChild->GetFName() == SocketName)
			{
				return Cast<UHandSocketComponent>(AttachChild); // Возвращаем найденный компонент
			}
		}
	}

	return nullptr; // Компонент не найден
}

/////////////////////////////////////////////////
//- Push networking getter / setter functions
//- Push networking геттеры / сеттеры
/////////////////////////////////////////////////

void UHandSocketComponent::SetRepGameplayTags(bool bNewRepGameplayTags)
{
	bRepGameplayTags = bNewRepGameplayTags;
#if WITH_PUSH_MODEL
	// Помечаем свойство как измененное для Push Model репликации
	MARK_PROPERTY_DIRTY_FROM_NAME(UHandSocketComponent, bRepGameplayTags, this);
#endif
}

void UHandSocketComponent::SetReplicateMovement(bool bNewReplicateMovement)
{
	bReplicateMovement = bNewReplicateMovement;
#if WITH_PUSH_MODEL
	// Помечаем свойство как измененное для Push Model репликации
	MARK_PROPERTY_DIRTY_FROM_NAME(UHandSocketComponent, bReplicateMovement, this);
#endif
}

FGameplayTagContainer& UHandSocketComponent::GetGameplayTags()
{
#if WITH_PUSH_MODEL
	// Если репликация тегов включена, помечаем свойство GameplayTags как измененное при доступе
	if (bRepGameplayTags)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UHandSocketComponent, GameplayTags, this);
	}
#endif

	return GameplayTags;
}

/////////////////////////////////////////////////
//- End Push networking getter / setter functions
//- Конец Push networking геттеров / сеттеров
/////////////////////////////////////////////////

void UHandSocketAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation(); // Вызываем базовую функцию

	// Получаем родительский компонент (должен быть UHandSocketComponent)
	OwningSocket = Cast<UHandSocketComponent>(GetOwningComponent()->GetAttachParent());
}
