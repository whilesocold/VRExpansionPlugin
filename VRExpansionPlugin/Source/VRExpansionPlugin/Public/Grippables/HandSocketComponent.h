// All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayTagAssetInterface.h"
#include "Components/SceneComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/BoneReference.h"
#include "Misc/Guid.h"

#include "HandSocketComponent.generated.h"

class UGripMotionControllerComponent;
class USkeletalMeshComponent;
class UPoseableMeshComponent;
class USkeletalMesh;
class UAnimSequence;
struct FPoseSnapshot;

DECLARE_LOG_CATEGORY_EXTERN(LogVRHandSocketComponent, Log, All);

// Custom serialization version for the hand socket component
// Пользовательская версия сериализации для компонента сокета руки
struct VREXPANSIONPLUGIN_API FVRHandSocketCustomVersion
{
    enum Type {
        // Before any version changes were made in the plugin
        // До внесения каких-либо изменений в версию плагина
        BeforeCustomVersionWasAdded = 0,

        // Added a set state tracker to handle in editor construction edge cases
        // Добавлен трекер состояния для обработки крайних случаев при конструировании в редакторе
        HandSocketStoringSetState = 1,

        // -----<new versions can be added above this line>-------------------------------------------------
        // -----<новые версии могут быть добавлены выше этой строки>-------------------------------------------------
        VersionPlusOne,
        LatestVersion = VersionPlusOne - 1
    };

    // The GUID for this custom version number
    // GUID для этого пользовательского номера версии
    const static FGuid GUID;

private:
    FVRHandSocketCustomVersion() {}
};

UENUM(BlueprintType)
namespace EVRAxis {
enum Type {
    X,
    Y,
    Z
};
}  // namespace EVRAxis

/**
 * A base class for custom hand socket objects
 * Not directly blueprint spawnable as you are supposed to subclass this to add on top your own custom data
 * Базовый класс для пользовательских объектов сокета руки
 * Не может быть создан напрямую в Blueprint, так как предполагается, что вы будете наследоваться от него для добавления своих
 * пользовательских данных
 */

USTRUCT(BlueprintType, Category = "VRExpansionLibrary")
struct VREXPANSIONPLUGIN_API FBPVRHandPoseBonePair
{
    GENERATED_BODY()
public:
    // Distance to offset to get center of waist from tracked parent location
    // Расстояние для смещения, чтобы получить центр талии от отслеживаемого родительского местоположения (Неправильный комментарий?
    // Относится к имени кости) Имя кости
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
    FName BoneName;

    // Initial "Resting" location of the tracker parent, assumed to be the calibration zero
    // Начальное "покоящееся" положение родительского трекера, предполагается, что это калибровочный ноль (Неправильный комментарий?
    // Относится к дельта-позе) Дельта-поза (относительное вращение) для этой кости
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Settings")
    FQuat DeltaPose;

    // Ссылка на кость для конструирования в редакторе
    FBoneReference ReferenceToConstruct;

    FBPVRHandPoseBonePair()
    {
        BoneName = NAME_None;
        DeltaPose = FQuat::Identity;
    }

    // Перегрузка оператора сравнения для поиска по имени кости
    FORCEINLINE bool operator==(const FName& Other) const { return (BoneName == Other); }
};

UCLASS(Blueprintable, ClassGroup = (VRExpansionPlugin),
    hideCategories = ("Component Tick", Events, Physics, Lod, "Asset User Data", Collision))
class VREXPANSIONPLUGIN_API UHandSocketComponent : public USceneComponent, public IGameplayTagAssetInterface {
    GENERATED_BODY()

public:
    UHandSocketComponent(const FObjectInitializer& ObjectInitializer);
    ~UHandSocketComponent();

    // static get socket compoonent // Статический метод для получения компонента сокета (Комментарий не соответствует коду)

    // Axis to mirror on for this socket
    //  Ось, по которой отзеркаливать для этого сокета
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Mirroring|Advanced")
    TEnumAsByte<EVRAxis::Type> MirrorAxis;

    // Axis to flip on when mirroring this socket
    // Ось, по которой инвертировать при отзеркаливании этого сокета
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Mirroring|Advanced")
    TEnumAsByte<EVRAxis::Type> FlipAxis;

    // Relative placement of the hand to this socket
    // Относительное размещение руки к этому сокету
    UPROPERTY(EditAnywhere, BlueprintReadWrite, /*DuplicateTransient,*/ Category = "Hand Socket Data")
    FTransform HandRelativePlacement;

    // Target Slot Prefix
    // Префикс целевого слота (вероятно, для анимации)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
    FName SlotPrefix;

    // If true the hand meshes relative transform will be de-coupled from the hand socket
    // Если true, относительная трансформация меша руки будет отсоединена от сокета руки
    UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hand Socket Data")
    bool bDecoupleMeshPlacement;

    // If true we should only be used to snap mesh to us, not for the actual socket transform
    // Will act like free gripping but the mesh will snap into position
    // Если true, мы должны использоваться только для привязки меша к нам, а не для фактической трансформации сокета
    // Будет действовать как свободный захват, но меш будет привязан к позиции
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
    bool bOnlySnapMesh;

    // If true the end user should only pull the hand pose, not its transform from this component
    // This is up to the end user to make use of as its part of the query steps.
    // Если true, конечный пользователь должен извлекать из этого компонента только позу руки, а не ее трансформацию
    // Использование этого остается на усмотрение конечного пользователя, так как это часть шагов запроса.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
    bool bOnlyUseHandPose;

    // If true we will not create the mesh relative transform using the attach socket we are attached too
    // Useful in cases where you aren't doing per bone gripping but want the socket to follow a bone that is animating
    // Если true, мы не будем создавать относительную трансформацию меша, используя сокет прикрепления, к которому мы присоединены
    // Полезно в случаях, когда вы не делаете захват для каждой кости, но хотите, чтобы сокет следовал за анимирующейся костью
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data")
    bool bIgnoreAttachBone;

    // If true then this socket is left hand dominant and will flip for the right hand instead
    // Если true, то этот сокет доминирует для левой руки и будет инвертирован для правой руки
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hand Socket Data")
    bool bLeftHandDominant;

    // If true we will mirror ourselves automatically for the off hand
    // Если true, мы будем автоматически отзеркаливаться для другой руки
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Mirroring", meta = (DisplayName = "Flip For Off Hand"))
    bool bFlipForLeftHand;

    // If true, when we mirror the hand socket it will only mirror rotation, not position
    // Если true, при отзеркаливании сокета руки будет отзеркалено только вращение, а не позиция
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Mirroring",
        meta = (editcondition = "bFlipForLeftHand"))  // Неточное имя в editcondition?
    bool bOnlyFlipRotation;

    // If true then this hand socket will always be considered "in range" and checked against others for lowest distance
    // Если true, этот сокет руки всегда будет считаться "в пределах досягаемости" и сравниваться с другими по наименьшему расстоянию
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Searching")
    bool bAlwaysInRange;

    // If true and there are multiple hand socket components in range with this setting
    // Then the default behavior will compare closest rotation on them all to pick one
    // Если true и в пределах досягаемости есть несколько компонентов сокета руки с этой настройкой
    // То поведение по умолчанию будет сравнивать ближайшее вращение между ними всеми, чтобы выбрать один
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Searching")
    bool bMatchRotation;

    // If true then the hand socket will not be considered for search operations
    // Если true, сокет руки не будет учитываться при операциях поиска
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Control")
    bool bDisabled;

    /***
    //	If true then the hand socket will be locked in place during gameplay and not moved with the actor (saving performance)
    //  Generally you want this unless you are moving a hand socket manually during play for custom grip offsetting logic
    //  If you need the relative location of the hand socket for game logic, get the LockedRelativeTransform variable if bLockInPlace is
    enabled.
    //  Defaulted off currently for bug testing
    //	Если true, то сокет руки будет зафиксирован на месте во время игрового процесса и не будет перемещаться вместе с актором (экономия
    производительности)
    //  Обычно это желательно, если только вы не перемещаете сокет руки вручную во время игры для пользовательской логики смещения захвата
    //  Если вам нужно относительное местоположение сокета руки для игровой логики, получите переменную LockedRelativeTransform, если
    bLockInPlace включен.
    //  В настоящее время по умолчанию выключено для тестирования ошибок
    ***/
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Hand Socket Data|Control")
    bool bLockInPlace;

    // Snap distance to use if you want to override the defaults.
    // Will be ignored if == 0.0f or bAlwaysInRange is true
    // Расстояние привязки для использования, если вы хотите переопределить значения по умолчанию.
    // Будет проигнорировано, если == 0.0f или bAlwaysInRange истинно
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Searching")
    float OverrideDistance;

    // If true we are expected to have a list of custom deltas for bones to overlay onto our base pose
    // Если true, ожидается, что у нас будет список пользовательских дельт для костей для наложения поверх нашей базовой позы
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation")
    bool bUseCustomPoseDeltas;

    // Custom rotations that are added on top of an animations bone rotation to make a final transform
    // Пользовательские вращения, которые добавляются поверх вращения кости анимации для создания финальной трансформации
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation")
    TArray<FBPVRHandPoseBonePair> CustomPoseDeltas;

    // Primary hand animation, for both hands if they share animations, right hand if they don't
    // If using a custom pose delta this is expected to be the base pose
    // Основная анимация руки, для обеих рук, если они используют общие анимации, для правой руки, если нет
    // Если используется пользовательская дельта позы, ожидается, что это будет базовая поза
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation")
    TObjectPtr<UAnimSequence> HandTargetAnimation;

    // Scale to apply when mirroring the hand, adjust to visualize your off hand correctly
    // Масштаб, применяемый при отзеркаливании руки, настройте для корректной визуализации другой руки
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Socket Data|Mirroring")
    FVector MirroredScale;

    // Единый оффсет между "авторским" фреймом слота на предмете и hand-фреймом кости руки.
    // По умолчанию Yaw=180, чтобы больше не крутить HandRelativePlacement на каждом предмете. Добавлено из-за UBIK
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Hand Socket Data|Authoring")
    FRotator AuthoringToHandRotationOffset = FRotator(0.f, 180.f, 0.f);

#if WITH_EDITORONLY_DATA
    // If true we will attempt to only show editing widgets for bones matching the _l or _r postfixes
    // Если true, мы попытаемся показывать виджеты редактирования только для костей, соответствующих постфиксам _l или _r
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation|Misc")
    bool bFilterBonesByPostfix;

    // The postfix to filter by
    // Постфикс для фильтрации
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation|Misc")
    FString FilterPostfix;

    /**
 * [НОВОЕ] Включает продвинутый режим предпросмотра для Full Body скелетов.
 * Когда активно, визуализатор будет вычислять смещение от пивота меша до кисти на основе
 * ТЕКУЩЕЙ примененной позы, а не референсной. Это обеспечивает полное соответствие (WYSIWYG)
 * между тем, что вы видите в редакторе, и тем, что будет в игре для IK-систем.
 * Включайте этот флаг для всех сокетов на предметах, предназначенных для Full Body персонажа.
 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization|Full Body")
    bool bUseAdvancedFullBodyPreview;

    /** Имя корневой кости кисти внутри меша визуализации (например, 'hand_r'). 
 *  Используется для корректного смещения full-body меша в режиме редактирования позы. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
    FName HandRootBoneNameForPose;

    // An array of bones to skip when looking to edit deltas, can help clean up the interaction if you have extra bones in the heirarchy
    // Массив костей для пропуска при поиске дельт для редактирования, может помочь очистить взаимодействие, если у вас есть лишние кости в
    // иерархии
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Animation|Misc")
    TArray<FName> BonesToSkip;

    // Получает трансформацию кости в определенный момент времени анимации
    FTransform GetBoneTransformAtTime(UAnimSequence* MyAnimSequence, /*float AnimTime,*/ int BoneIdx, FName BoneName, bool bUseRawDataOnly);
#endif

    // Returns the base target animation of the hand (if there is one)
    // Возвращает базовую целевую анимацию руки (если она есть)
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
    UAnimSequence* GetTargetAnimation();

    /**
     * Returns the target animation of the hand blended with the delta rotations if there are any
     * @param PoseSnapShot - Snapshot generated by this function
     * @param TargetMesh - Targetmesh to check the skeleton of
     * @param bSkipRootBone - If true we will skip the root bone (IE: Hand_r) and only apply the children poses (Full body)
     * @param bFlipHand - If true we will mirror the pose, this is primarily to apply to a left hand from a right
     * Возвращает целевую анимацию руки, смешанную с дельта-вращениями, если они есть
     * @param PoseSnapShot - Снимок позы, сгенерированный этой функцией
     * @param TargetMesh - Целевой меш для проверки скелета
     * @param bSkipRootBone - Если true, мы пропустим корневую кость (например, Hand_r) и применим только позы дочерних костей (Полное тело)
     * @param bFlipHand - Если true, мы отзеркалим позу, это в первую очередь для применения к левой руке от правой
     */
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
    bool GetBlendedPoseSnapShot(
        FPoseSnapshot& PoseSnapShot, USkeletalMeshComponent* TargetMesh = nullptr, bool bSkipRootBone = false, bool bFlipHand = false);

    /**
     * Converts an animation sequence into a pose snapshot
     * @param InAnimationSequence - Sequence to convert to a pose snapshot
     * @param OutPoseSnapShot - Snapshot returned by this function
     * @param TargetMesh - Targetmesh to check the skeleton of
     * @param bSkipRootBone - If true we will skip the root bone (IE: Hand_r) and only apply the children poses (Full body)
     * @param bFlipHand - If true we will mirror the pose, this is primarily to apply to a left hand from a right
     * Преобразует анимационную последовательность в снимок позы
     * @param InAnimationSequence - Последовательность для преобразования в снимок позы
     * @param OutPoseSnapShot - Снимок позы, возвращаемый этой функцией
     * @param TargetMesh - Целевой меш для проверки скелета
     * @param bSkipRootBone - Если true, мы пропустим корневую кость (например, Hand_r) и применим только позы дочерних костей (Полное тело)
     * @param bFlipHand - Если true, мы отзеркалим позу, это в первую очередь для применения к левой руке от правой
     */
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data", meta = (bIgnoreSelf = "true"))
    static bool GetAnimationSequenceAsPoseSnapShot(UAnimSequence* InAnimationSequence, FPoseSnapshot& OutPoseSnapShot,
        USkeletalMeshComponent* TargetMesh = nullptr, bool bSkipRootBone = false, bool bFlipHand = false);

    /**
     * Gets all hand socket components in the entire level (this is a slow operation, DO NOT run this on tick)
     * Получает все компоненты сокета руки на всем уровне (это медленная операция, НЕ запускайте ее в Tick)
     */
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
    static void GetAllHandSocketComponents(TArray<UHandSocketComponent*>& OutHandSockets);

    /**
     * Gets all hand socket components within a set range of a world location (this is a slow operation, DO NOT run this on tick)
     * Получает все компоненты сокета руки в заданном диапазоне от мирового местоположения (это медленная операция, НЕ запускайте ее в Tick)
     */
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
    static bool GetAllHandSocketComponentsInRange(
        FVector SearchFromWorldLocation, float SearchRange, TArray<UHandSocketComponent*>& OutHandSockets);

    /**
     * Gets the closest hand socket component within a set range of a world location (this is a slow operation, DO NOT run this on tick)
     * Must check the output for validity
     * Получает ближайший компонент сокета руки в заданном диапазоне от мирового местоположения (это медленная операция, НЕ запускайте ее в
     * Tick) Необходимо проверить возвращаемое значение на валидность
     */
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
    static UHandSocketComponent* GetClosestHandSocketComponentInRange(FVector SearchFromWorldLocation, float SearchRange);

    // Returns the target relative transform of the hand
    // Возвращает целевую относительную трансформацию руки
    // UFUNCTION(BlueprintCallable, Category = "Hand Socket Data") // Закомментировано в оригинале
    FTransform GetHandRelativePlacement();

    // Вспомогательная функция для отзеркаливания трансформации руки
    inline void MirrorHandTransform(FTransform& ReturnTrans, FTransform& relTrans)
    {
        // Если отзеркаливаем только вращение
        if (bOnlyFlipRotation)
        {
            ReturnTrans.SetTranslation(ReturnTrans.GetTranslation() - relTrans.GetTranslation());  // Убираем относительное смещение
            ReturnTrans.Mirror(GetAsEAxis(MirrorAxis), GetCrossAxis());                            // Отзеркаливаем вращение
            ReturnTrans.SetTranslation(ReturnTrans.GetTranslation() + relTrans.GetTranslation());  // Возвращаем относительное смещение
        }
        else  // Иначе отзеркаливаем и позицию, и вращение
        {
            ReturnTrans.Mirror(GetAsEAxis(MirrorAxis), GetCrossAxis());
        }
    }

    // Вспомогательная функция для преобразования EVRAxis в EAxis
    inline TEnumAsByte<EAxis::Type> GetAsEAxis(TEnumAsByte<EVRAxis::Type> InAxis)
    {
        switch (InAxis)
        {
            case EVRAxis::X: {
                return EAxis::X;
            }
            break;
            case EVRAxis::Y: {
                return EAxis::Y;
            }
            break;
            case EVRAxis::Z: {
                return EAxis::Z;
            }
            break;
        }

        return EAxis::X;  // По умолчанию
    }

    // Вспомогательная функция для получения вектора оси отзеркаливания
    inline FVector GetMirrorVector()
    {
        switch (MirrorAxis)
        {
            case EVRAxis::Y: {
                return FVector::RightVector;
            }
            break;
            case EVRAxis::Z: {
                return FVector::UpVector;
            }
            break;
            case EVRAxis::X:
            default: {
                return FVector::ForwardVector;
            }
            break;
        }
    }

    // Вспомогательная функция для получения вектора оси инвертирования
    inline FVector GetFlipVector()
    {
        switch (FlipAxis)
        {
            case EVRAxis::Y: {
                return FVector::RightVector;
            }
            break;
            case EVRAxis::Z: {
                return FVector::UpVector;
            }
            break;
            case EVRAxis::X:
            default: {
                return FVector::ForwardVector;
            }
            break;
        }
    }

    // Вспомогательная функция для получения оси, перпендикулярной оси отзеркаливания и оси инвертирования
    inline TEnumAsByte<EAxis::Type> GetCrossAxis()
    {
        // Проверяем знак теперь, чтобы избежать возможных проблем с точностью на мобильных устройствах
        FVector SignVec = MirroredScale.GetSignVector();

        if (SignVec.X < 0)
        {
            return EAxis::X;
        }
        else if (SignVec.Z < 0)
        {
            return EAxis::Z;
        }
        else if (SignVec.Y < 0)
        {
            return EAxis::Y;
        }

        return GetAsEAxis(FlipAxis);  // Возвращаем ось инвертирования как запасной вариант? Неясно.

        /* // Закомментированный старый способ определения поперечной оси
        if (FlipAxis == EVRAxis::Y)
        {
            return EAxis::Z;
        }
        else if (FlipAxis == EVRAxis::Z)
        {
            return EAxis::X;
        }
        else if (FlipAxis == EVRAxis::X)
        {
            return EAxis::Y;
        }*/

        // return EAxis::None;
    }
    // Returns the target relative transform of the hand to the gripped object
    // If you want the transform mirrored you need to pass in which hand is requesting the information
    // If UseParentScale is true then we will scale the value by the parent scale (generally only for when not using absolute hand scale)
    // If UseMirrorScale is true then we will mirror the scale on the hand by the hand sockets mirror scale when appropriate (not for fully
    // body!) if UseMirrorScale is false than the resulting transform will not have mirroring scale added so you may have to break the
    // transform. Возвращает целевую относительную трансформацию руки к захваченному объекту Если вы хотите, чтобы трансформация была
    // отзеркалена, вам нужно передать, какая рука запрашивает информацию Если UseParentScale истинно, то мы масштабируем значение на
    // масштаб родителя (обычно только когда не используется абсолютный масштаб руки) Если UseMirrorScale истинно, то мы отзеркалим масштаб
    // руки на основе масштаба отзеркаливания сокета руки, когда это уместно (не для полного тела!) если UseMirrorScale ложно, то
    // результирующая трансформация не будет иметь добавленного масштаба отзеркаливания, так что вам, возможно, придется разбить
    // трансформацию.
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
    FTransform GetMeshRelativeTransform(bool bIsRightHand, bool bUseParentScale = false, bool bUseMirrorScale = false);

    // Returns the defined hand socket component (if it exists, you need to valid check the return!
    // If it is a valid return you can then cast to your projects base socket class and handle whatever logic you want
    // Возвращает определенный компонент сокета руки (если он существует, вам нужно проверить возвращаемое значение на валидность!
    // Если возвращаемое значение валидно, вы можете привести его к базовому классу сокета вашего проекта и обработать любую желаемую логику
    UFUNCTION(BlueprintCallable, Category = "Hand Socket Data")
    static UHandSocketComponent* GetHandSocketComponentFromObject(UObject* ObjectToCheck, FName SocketName);

    // Получает трансформацию сокета руки для заданного контроллера захвата
    virtual FTransform GetHandSocketTransform(UGripMotionControllerComponent* QueryController, bool bIgnoreOnlySnapMesh = false);

#if WITH_EDITOR
    // Вызывается после изменения свойства в редакторе
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
#if WITH_EDITORONLY_DATA
    // Добавляет объекты, на которые ссылается данный объект, в сборщик мусора (только для редактора)
    static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
    // Вызывается при уничтожении компонента (только для редактора)
    virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
    // Преобразует визуализацию позы в анимацию (только для редактора)
    void PoseVisualizationToAnimation(bool bForceRefresh = false);
    // Флаг, указывающий, была ли поза обновлена в тике (только для редактора)
    bool bTickedPose;

    // Флаг, указывающий, отсоединен ли компонент (только для редактора)
    UPROPERTY()
    bool bDecoupled;

#endif
    // Переопределение для пользовательской сериализации
    virtual void Serialize(FArchive& Ar) override;
    // Вызывается при регистрации компонента
    virtual void OnRegister() override;
    // [ДОБАВЛЕНО] Вызывается при снятии компонента с регистрации
    virtual void OnUnregister() override;
    
    // Вызывается перед репликацией свойств
    virtual void PreReplication(IRepChangedPropertyTracker& ChangedPropertyTracker) override;

    // ------------------------------------------------
    // Gameplay tag interface
    // Интерфейс Gameplay тегов
    // ------------------------------------------------

    /** Overridden to return requirements tags */
    // Переопределено для возврата тегов требований (ошибка в комментарии? возвращает имеющиеся теги)
    // Возвращает Gameplay теги, установленные на этом объекте
    virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override { TagContainer = GameplayTags; }

protected:
    /** Tags that are set on this object */
    // Теги, установленные на этом объекте
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "GameplayTags")
    FGameplayTagContainer GameplayTags;

    // End Gameplay Tag Interface
    // Конец интерфейса Gameplay тегов

    // Requires bReplicates to be true for the component
    // Требует, чтобы bReplicates было true для компонента
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
    bool bRepGameplayTags;

    // Overrides the default of : true and allows for controlling it like in an actor, should be default of off normally with grippable
    // components Переопределяет значение по умолчанию : true и позволяет управлять им как в акторе, обычно должно быть выключено по
    // умолчанию для захватываемых компонентов
    UPROPERTY(EditAnywhere, Replicated, BlueprintReadWrite, Category = "VRGripInterface|Replication")
    bool bReplicateMovement;

public:
    // Получает контейнер Gameplay тегов
    FGameplayTagContainer& GetGameplayTags();

    // Устанавливает, будут ли реплицироваться Gameplay теги
    void SetRepGameplayTags(bool NewRepGameplayTags);
    // Получает флаг репликации Gameplay тегов
    inline bool GetRepGameplayTags() { return bRepGameplayTags; };
    // Устанавливает, будет ли реплицироваться движение компонента
    void SetReplicateMovement(bool NewReplicateMovement);
    // Получает флаг репликации движения
    inline bool GetReplicateMovement() { return bReplicateMovement; };

    /** mesh component to indicate hand placement */
    // Компонент меша для обозначения размещения руки
#if WITH_EDITORONLY_DATA

    // Компонент для визуализации позы руки в редакторе
    UPROPERTY()
    TObjectPtr<UPoseableMeshComponent> HandVisualizerComponent;

    // Скелетный меш для визуализации
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
    TObjectPtr<USkeletalMesh> VisualizationMesh;

    // Если мы должны показывать меш визуализации
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
    bool bShowVisualizationMesh;

    // Показывать визуализацию отзеркаленной
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
    bool bMirrorVisualizationMesh;

    // Если мы должны показывать диапазон захвата этого сокета (показывает текст, если всегда в диапазоне)
    // Если OverrideDistance равно нулю, то он пытается определить значение из родительской архитектуры
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hand Visualization")
    bool bShowRangeVisualization;

    // Позиционирует меш визуализации
    void PositionVisualizationMesh();

    // Объявляем нашу новую функцию визуализации
    void PositionFullBodyVisualizationMesh();
    // Скрывает меш визуализации
    void HideVisualizationMesh();

#endif

#if WITH_EDITORONLY_DATA
    // Material to apply to the hand
    // Материал для применения к руке (для визуализации)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hand Visualization")
    TObjectPtr<UMaterialInterface> HandPreviewMaterial;

#endif
};

UCLASS(transient, Blueprintable, hideCategories = AnimInstance, BlueprintType)
class VREXPANSIONPLUGIN_API UHandSocketAnimInstance : public UAnimInstance {
    GENERATED_BODY()

public:
    // Указатель на компонент сокета, владеющий этим AnimInstance
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, transient, Category = "Socket Data")
    TObjectPtr<UHandSocketComponent> OwningSocket;

    // Вызывается при инициализации анимации
    virtual void NativeInitializeAnimation() override;
};
