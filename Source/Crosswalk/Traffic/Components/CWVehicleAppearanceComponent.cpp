// CWVehicleAppearanceComponent.cpp

#include "Traffic/Components/CWVehicleAppearanceComponent.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Traffic/CWBaseVehicle.h"
#include "Traffic/CWTrafficLog.h"
#include "Traffic/Data/CWVehicleMeshVariant.h"

UCWVehicleAppearanceComponent::UCWVehicleAppearanceComponent()
{
	// 바퀴 회전은 차량 Tick 이 부른다. 자기 틱은 없다. (헤더 주석 참고)
	PrimaryComponentTick.bCanEverTick = false;

	// VariantPick 을 복제하려면 컴포넌트 자체가 복제 대상이어야 한다.
	SetIsReplicatedByDefault(true);
}

void UCWVehicleAppearanceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 스폰 직후 한 번 정해지고 끝이라 초기 번치 이후로는 오가지 않는다.
	DOREPLIFETIME(UCWVehicleAppearanceComponent, VariantPick);
}

ACWBaseVehicle* UCWVehicleAppearanceComponent::GetVehicle() const
{
	return Cast<ACWBaseVehicle>(GetOwner());
}

const FCWVehicleMeshVariant* UCWVehicleAppearanceComponent::PickVariant()
{
	ACWBaseVehicle* Vehicle = GetVehicle();
	if (!Vehicle || !Vehicle->HasAuthority())
	{
		return nullptr;
	}

	const TArray<FCWVehicleMeshVariant>& MeshVariants = Vehicle->GetMeshVariants();
	if (MeshVariants.Num() == 0)
	{
		return nullptr;
	}

	const int32 FamilyIndex = FMath::RandHelper(MeshVariants.Num());
	const int32 ColorCount = MeshVariants[FamilyIndex].ColorMeshes.Num();

	VariantPick.Family = static_cast<uint8>(FamilyIndex);
	VariantPick.Color = ColorCount > 0 ? static_cast<uint8>(FMath::RandHelper(ColorCount)) : 0;

	ApplyMeshVariant();

	return &MeshVariants[FamilyIndex];
}

const FCWVehicleMeshVariant* UCWVehicleAppearanceComponent::GetActiveVariant() const
{
	const ACWBaseVehicle* Vehicle = GetVehicle();
	if (!Vehicle || !VariantPick.IsPicked())
	{
		return nullptr;
	}

	const TArray<FCWVehicleMeshVariant>& MeshVariants = Vehicle->GetMeshVariants();

	return MeshVariants.IsValidIndex(VariantPick.Family) ? &MeshVariants[VariantPick.Family] : nullptr;
}

void UCWVehicleAppearanceComponent::ApplyMeshVariant()
{
	ACWBaseVehicle* Vehicle = GetVehicle();
	const FCWVehicleMeshVariant* Variant = GetActiveVariant();
	if (!Vehicle || !Variant)
	{
		return;
	}

	UStaticMeshComponent* VehicleMesh = Vehicle->GetVehicleMesh();
	UPoseableMeshComponent* VehicleSkeletalMesh = Vehicle->GetVehicleSkeletalMesh();
	if (!VehicleMesh || !VehicleSkeletalMesh)
	{
		return;
	}

	/**
	 * 뽑은 색의 메쉬. 색 목록이 비었거나 그 칸이 비어 있으면 널이고, 그러면 아래에서
	 * 정적 메쉬 쪽으로 떨어진다. 양쪽 다 비면 BP 가 물려 둔 기본 메쉬가 그대로 남는다.
	 */
	USkeletalMesh* ColorMesh = Variant->ColorMeshes.IsValidIndex(VariantPick.Color)
		? Variant->ColorMeshes[VariantPick.Color]
		: nullptr;

	/**
	 * 지정한 쪽을 채우고 반대쪽은 비운다. 둘 다 남겨 두면 BP 가 물려 둔 기본 메쉬와
	 * 변형 메쉬가 한 자리에 겹쳐 보인다. 둘 다 비어 있을 때만 아무것도 건드리지 않는다.
	 *
	 * SetSkeletalMeshAsset 이 아니라 SetSkinnedAssetAndUpdate 를 쓰는 이유는 앞의 것이
	 * USkeletalMeshComponent 의 함수이기 때문이다. 포저블 메쉬는 그 아래 USkinnedMeshComponent
	 * 를 상속하고, 본 배열 재할당(AllocateTransformData)은 이쪽이 대신 불러 준다.
	 */
	if (ColorMesh)
	{
		VehicleSkeletalMesh->SetSkinnedAssetAndUpdate(ColorMesh);
		VehicleMesh->SetStaticMesh(nullptr);
	}
	else if (Variant->Mesh)
	{
		VehicleMesh->SetStaticMesh(Variant->Mesh);
		VehicleSkeletalMesh->SetSkinnedAssetAndUpdate(nullptr);
	}

	/**
	 * 콜리전도 같이 바꾼다. 크기 값은 외형 데이터가 들고 있지만 적용은 차량이 한다 -
	 * 판정 몸체를 만지는 유일한 자리를 차량 안에 두기 위해서다.
	 *
	 * 같은 승용차 팩 안에서도 폭이 178~253cm 로 벌어져서, 박스를 하나로 고정하면
	 * 넓은 차는 옆구리 판정이 비고 좁은 차는 스치기만 해도 맞는다.
	 *
	 * 이 값은 GetVehicleLength 를 통해 스포너의 간격 계산에도 들어간다. 다만 스폰 주기가
	 * 차 길이에 비해 훨씬 길어서(8초면 6000cm 이동) 실제로 간격 제한에 걸리는 일은 없다.
	 */
	if (!Variant->BoxExtent.IsNearlyZero())
	{
		Vehicle->ApplyCollisionExtent(Variant->BoxExtent);
	}

	// 메쉬가 바뀌었으니 바퀴 본의 레퍼런스 자세를 다시 받는다. 지금이 포즈가 손타지 않은 유일한 시점이다.
	CacheWheelBoneRestRotations();
}

float UCWVehicleAppearanceComponent::GetActiveWheelRadius() const
{
	const FCWVehicleMeshVariant* Variant = GetActiveVariant();
	if (Variant && Variant->WheelRadius > 0.f)
	{
		return Variant->WheelRadius;
	}

	const ACWBaseVehicle* Vehicle = GetVehicle();

	return Vehicle ? Vehicle->GetDefaultWheelRadius() : 0.f;
}

const TArray<FName>& UCWVehicleAppearanceComponent::GetActiveWheelBones() const
{
	const FCWVehicleMeshVariant* Variant = GetActiveVariant();
	if (Variant && Variant->WheelBones.Num() > 0)
	{
		return Variant->WheelBones;
	}

	if (const ACWBaseVehicle* Vehicle = GetVehicle())
	{
		return Vehicle->GetDefaultWheelBones();
	}

	static const TArray<FName> Empty;

	return Empty;
}

void UCWVehicleAppearanceComponent::CacheWheelBoneRestRotations()
{
	CachedWheelBones.Reset();
	WheelBoneRestRotations.Reset();
	bWheelRestCached = true;

	const ACWBaseVehicle* Vehicle = GetVehicle();
	UPoseableMeshComponent* VehicleSkeletalMesh = Vehicle ? Vehicle->GetVehicleSkeletalMesh() : nullptr;
	if (!VehicleSkeletalMesh || !VehicleSkeletalMesh->GetSkinnedAsset())
	{
		return;
	}

	for (const FName& BoneName : GetActiveWheelBones())
	{
		/**
		 * 없는 이름을 여기서 걸러낸다.
		 *
		 * 거르지 않고 매 틱 SetBoneRotationByName 에 넘기면 엔진이 그때마다 경고를 찍어
		 * 차량 수 x 바퀴 수 x 프레임만큼 로그가 쌓인다. 한 번만 확인하고 목록에서 빼면
		 * 경고도 스폰당 한 줄로 끝난다.
		 */
		if (VehicleSkeletalMesh->GetBoneIndex(BoneName) == INDEX_NONE)
		{
			/**
			 * 같은 (메쉬, 본) 조합은 판당 한 번만 찍는다.
			 *
			 * 스폰당 한 줄이어도 차는 계속 스폰된다 - 09-16 로그에서 오토바이 한 종류가
			 * 408줄을 냈다. 이건 그 차 인스턴스의 문제가 아니라 **데이터의 문제**라서,
			 * 같은 말을 400번 해도 새로 알게 되는 것이 없다. 고칠 사람에게 필요한 정보는
			 * "어느 메쉬의 어느 본 이름이 틀렸나" 한 줄이 전부다. 그래서 인스턴스 이름
			 * 대신 메쉬 이름으로 묶는다.
			 */
			static TSet<TPair<FName, FName>> ReportedMissingWheelBones;

			const USkinnedAsset* SkinnedAsset = VehicleSkeletalMesh->GetSkinnedAsset();
			const FName MeshName = SkinnedAsset ? SkinnedAsset->GetFName() : NAME_None;

			bool bAlreadyReported = false;
			ReportedMissingWheelBones.Add(TPair<FName, FName>(MeshName, BoneName), &bAlreadyReported);

			if (!bAlreadyReported)
			{
				UE_LOG(LogCWTraffic, Warning, TEXT("[Traffic] 메쉬 '%s' 에 바퀴 본 '%s' 이 없다. 이 본은 건너뛴다. (차종 데이터의 WheelBones 확인)"),
					*MeshName.ToString(), *BoneName.ToString());
			}

			continue;
		}

		/**
		 * 레퍼런스 포즈의 컴포넌트 공간 회전을 받아 둔다.
		 *
		 * 메쉬를 막 씌운 직후라 본 포즈가 아직 레퍼런스 그대로다. 여기서 받지 않고
		 * 주행 중에 받으면 이미 우리가 돌려 놓은 각도가 섞여 들어온다.
		 */
		CachedWheelBones.Add(BoneName);
		WheelBoneRestRotations.Add(
			VehicleSkeletalMesh->GetBoneTransformByName(BoneName, EBoneSpaces::ComponentSpace).GetRotation());
	}
}

void UCWVehicleAppearanceComponent::UpdateWheelSpin(float DeltaSeconds)
{
	const ACWBaseVehicle* Vehicle = GetVehicle();
	if (!Vehicle)
	{
		return;
	}

	const float Radius = GetActiveWheelRadius();
	if (Radius <= KINDA_SMALL_NUMBER)
	{
		// 반지름이 0 인 차종은 바퀴를 돌리지 않는다. 정적 메쉬 차량이 여기로 들어온다.
		return;
	}

	/**
	 * 굴림 조건으로 각속도를 낸다. 바퀴가 원주만큼 나아가면 정확히 한 바퀴다.
	 *
	 *   각속도(도/초) = 이동속도 / 원주 * 360
	 *
	 * GetCurrentSpeed 는 매 Tick 이 채우는 값이라 서버·클라이언트 양쪽에 있다. 그래서
	 * 각 클라이언트가 스스로 굴려도 화면마다 크게 다르지 않고, 달라 봐야 바퀴 각도다.
	 */
	const float Circumference = 2.f * PI * Radius;
	WheelSpinDegrees += Vehicle->GetCurrentSpeed() / Circumference * 360.f * DeltaSeconds;

	// 무한히 키우면 부동소수 정밀도가 떨어져 회전이 눈에 띄게 튄다.
	WheelSpinDegrees = FMath::Fmod(WheelSpinDegrees, 360.f);

	ApplyWheelSpinToBones();
}

void UCWVehicleAppearanceComponent::ApplyWheelSpinToBones()
{
	if (!bWheelRestCached)
	{
		// 변형을 쓰지 않는 차종 BP 도 있다. 그쪽은 ApplyMeshVariant 를 타지 않으므로 여기서 채운다.
		CacheWheelBoneRestRotations();
	}

	if (CachedWheelBones.Num() == 0)
	{
		// 정적 메쉬 변형을 뽑았거나, 본 이름이 없거나, 넣은 이름이 메쉬에 없는 경우다.
		return;
	}

	const ACWBaseVehicle* Vehicle = GetVehicle();
	UPoseableMeshComponent* VehicleSkeletalMesh = Vehicle ? Vehicle->GetVehicleSkeletalMesh() : nullptr;
	if (!VehicleSkeletalMesh)
	{
		return;
	}

	/**
	 * 화면에 안 잡히는 동안은 본을 건드리지 않는다.
	 *
	 * 차량은 화면 밖에서 스폰돼 한참 달려온 뒤에 시야에 들어온다. 그동안의 바퀴 각도는
	 * 아무도 보지 않는데 포즈 갱신 비용은 그대로 든다. 각도 자체는 위에서 계속 누적하므로
	 * 다시 보이는 순간의 각도는 맞다. 안 보이는 동안의 중간 프레임만 건너뛴다.
	 *
	 * (스켈레탈 메쉬 컴포넌트의 VisibilityBasedAnimTickOption 이 하던 일을 옮겨온 것이다.
	 *  포저블 메쉬는 애님 틱을 돌지 않으므로 그 옵션이 없다.)
	 */
	if (!VehicleSkeletalMesh->WasRecentlyRendered())
	{
		return;
	}

	/**
	 * 레퍼런스 자세 위에 회전을 얹는다. 덮어쓰지 않는다.
	 *
	 * SetBoneRotationByName 은 본의 회전을 통째로 갈아 끼운다. 그래서 회전만 넘기면
	 * 바퀴가 원래 들고 있던 자세가 날아간다. MegaCity 팩은 바퀴 본이 옆을 보도록 돌아간
	 * 채로 바인드돼 있어서, 각도만 넣으면 바퀴가 누운 채로 돌았다 (2026-09-15 실기).
	 *
	 * 곱하는 순서가 중요하다. UE 는 A * B 를 "B 를 먼저, 그 다음 A" 로 해석하므로
	 * Spin * Rest 는 레퍼런스 자세를 잡은 뒤 그 위에서 돌리는 것이 된다. 반대로 쓰면
	 * 바퀴 자기 축을 기준으로 돌아 네 바퀴가 제각각 다른 방향으로 구른다.
	 *
	 * 축은 컴포넌트 공간의 Y 다. 차량이 +X 를 보고 서 있으니 바퀴 축이 Y 이고, 이 축은
	 * 본이 어떤 자세로 바인드됐든 상관없이 네 바퀴가 똑같이 쓴다.
	 *
	 * 부호가 음수인 이유는 Pitch 양수가 +Z 를 -X 로 보내기 때문이다. 바퀴 꼭대기는 차가
	 * 전진할 때 앞(+X)으로 가야 한다. 거꾸로 구르면 이 부호만 뒤집으면 된다.
	 */
	const FQuat Spin = FRotator(-WheelSpinDegrees, 0.f, 0.f).Quaternion();
	for (int32 Index = 0; Index < CachedWheelBones.Num(); ++Index)
	{
		const FQuat Rotation = Spin * WheelBoneRestRotations[Index];
		VehicleSkeletalMesh->SetBoneRotationByName(CachedWheelBones[Index], Rotation.Rotator(), EBoneSpaces::ComponentSpace);
	}
}

void UCWVehicleAppearanceComponent::OnRep_VariantPick()
{
	ApplyMeshVariant();
}
