#pragma once

#include "CoreMinimal.h"
#include "Painting/Zone/CWBaseWorkZone.h"
#include "Painting/Data/CWDustPatch.h"
#include "CWWorkZone_Dust.generated.h"

class UMaterialInterface;

/**
 * 1단계 도로 청소용 존. 빗자루로 쓸어서 먼지를 없앤다.
 *
 * 진행도는 부모가 들고 있고 서버 권위다. 여러 명이 같이 쓸면 각자 ApplyWork 를 부르므로
 * 자동으로 인원수만큼 빨라진다. 완료되면 HUD 체크리스트와 선행 단계 잠금이 함께 풀린다.
 *
 * 겉모습은 순수한 로컬 연출이다.
 *   각 클라이언트가 리플리케이트된 진행도만 보고 똑같이 그린다. 패치 배치를 만드는 난수 시드가
 *   존 이름에서 나오므로 별도 동기화 없이 모든 기계의 그림이 같다.
 *
 * 붓이 지나간 자리만 지우지 않는 이유
 *   예전에는 렌더 타깃 마스크에 붓 자취를 찍어 "쓴 자리만" 지웠다. 그런데 그러면 먼지가
 *   국소적으로 뻥 뚫려서, 쌓인 먼지가 아니라 스티커를 긁어낸 것처럼 보였다.
 *   지금은 싱크홀이 아스팔트를 맞고 단계별로 작아지는 것과 같은 방식이다.
 *   진행도가 오르면 패치가 정해진 순서로 하나씩 걷히고, 남은 패치는 전체적으로 옅어진다.
 *   덕분에 매 프레임 렌더 타깃에 그리던 비용과 Tick 이 통째로 없어졌다.
 *
 * 머티리얼 요구사항 (M_DustGround 참고)
 *   도메인이 Deferred Decal, 셰이딩 모델이 Default Lit 이어야 한다.
 *   Unlit 이면 조명 계산을 안 타서 그늘에서도 같은 밝기로 보이고, 그게 스티커처럼 보이는
 *   가장 큰 원인이다. Normal 을 GBuffer 에 써야 먼지 알갱이가 빛을 받아 입체감이 생긴다.
 *
 *   스칼라 파라미터 ClearedPct (존 전체 진행도) 와 PatchFade (패치 하나의 알파) 로 구동한다.
 *   Tiling / UVOffset / DustTint 는 패치마다 다른 값이 들어가 생김새를 흩뜨린다.
 *
 * 먼지가 메쉬가 아니라 데칼인 이유
 *   먼지는 도로 위에 얹히는 바닥 연출이고, 싱크홀 균열이나 페인트 자국과 같은 자리를 다툰다.
 *   반투명 평면 메쉬로 두면 층 순서를 정할 방법이 없다. 반투명은 데칼 패스보다 뒤에 그려지고
 *   디퍼드 데칼은 GBuffer 를 고치므로, 먼지가 항상 데칼 위로 올라가 균열을 덮어버린다.
 *   Receives Decals 를 꺼도 달라지지 않는다. 반투명 서피스는 애초에 데칼이 닿지 않는다.
 *
 *   데칼끼리는 SortOrder 로 층을 명시적으로 정할 수 있다. 그래서 먼지도 데칼로 두고
 *   DecalSortOrder 를 낮게 잡아 균열 밑으로 깔았다. 도로 요철을 타는 것과 Z-파이팅이
 *   없어지는 것은 덤이다. 입체감은 메쉬가 아니라 조명 반응에서 나온다.
 */
UCLASS()
class CROSSWALK_API ACWWorkZone_Dust : public ACWBaseWorkZone
{
	GENERATED_BODY()

public:
	ACWWorkZone_Dust();

	virtual void OnConstruction(const FTransform& Transform) override;

	virtual void BeginPlay() override;

	virtual void ApplyWork(UCWWorkerComponent* Worker, const FVector& WorldLocation, float DeltaSeconds) override;

protected:
	virtual void OnProgressChanged(float NewProgress) override;

	/**
	 * 패치 하나가 완전히 걷힌 순간. BP 에서 흙먼지를 한 번 터뜨리는 자리다.
	 *
	 * 이 연출이 없으면 패치가 조용히 사라져서 무슨 일이 일어났는지 안 보인다.
	 * "단계별로 줄어든다" 를 눈에 각인시키는 건 사실상 이 한 방이다.
	 * (ACWSinkhole::OnFillProgressed 와 같은 자리)
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Dust")
	void OnDustPatchCleared(int32 PatchIndex, const FVector& PatchLocation);
	virtual void OnDustPatchCleared_Implementation(int32 PatchIndex, const FVector& PatchLocation);

private:
	/** Area 안에 패치 데칼을 흩뿌린다. 에디터에서도 돌아서 배치를 눈으로 보고 잡을 수 있다. */
	void BuildPatches();

	/** 패치마다 MID 를 만들고 생김새를 흩뜨리는 파라미터를 넣는다. 런타임 전용이다. */
	void CreatePatchMaterials();

	/** 진행도를 패치별 알파로 바꿔 넣는다. 걷힌 패치는 렌더에서 아예 빠진다. */
	void ApplyProgressToPatches();

	/**
	 * 이 순번의 패치가 이 진행도에서 얼마나 남아 있는가. 1 이면 그대로, 0 이면 다 걷혔다.
	 *
	 * 패치마다 걷히는 구간을 조금씩 겹쳐 둔다. 구간을 딱 나누면 한 장이 다 사라진 뒤에야
	 * 다음 장이 움직여서 뚝뚝 끊기고, 완전히 겹치면 그냥 전체가 같이 옅어져 단계감이 없다.
	 */
	float ComputePatchFade(int32 ClearSlot, float InProgress) const;

	/**
	 * 배치를 만드는 난수 시드. 모든 기계에서 같은 값이 나와야 한다.
	 *
	 * FName 의 해시는 쓰면 안 된다. 이름 테이블에 등록된 순서에 따라 값이 달라져서
	 * 서버와 클라이언트가 다른 배치를 만든다. 문자열 CRC 는 내용만 보므로 안전하다.
	 */
	int32 GetLayoutSeed() const;

protected:
	// 혼자서 전부 쓸어내는 데 걸리는 시간(초). 두 명이 붙으면 절반으로 줄어든다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust", meta = (ClampMin = "0.1"))
	float SecondsToClear = 4.f;

	/**
	 * 먼지 패치에 쓸 데칼 머티리얼. 도메인이 Deferred Decal, 셰이딩 모델이 Default Lit 이어야 한다.
	 *
	 * 패치는 런타임에 만들어지므로 컴포넌트에 미리 지정해 둘 수가 없다. 반드시 여기에 넣어야 한다.
	 * EditDefaultsOnly 로 두면 레벨에 배치한 인스턴스에서 편집할 수 없어서 EditAnywhere 로 둔다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	TObjectPtr<UMaterialInterface> DustMaterial;

	/**
	 * 존 하나를 몇 장으로 쪼갤 것인가.
	 *
	 * 늘릴수록 실루엣이 잘게 부서지지만 데칼 개수만큼 드로우 셋업이 붙는다. 덮는 총 면적은
	 * 그대로라 픽셀 비용은 거의 같으니, 실루엣이 충분히 부서지는 선에서 멈추는 것이 이득이다.
	 * 단계별로 걷히는 칸 수이기도 해서, 너무 적으면 한 장이 사라질 때마다 뚝뚝 끊겨 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "1", ClampMax = "32"))
	int32 PatchCount = 8;

	/**
	 * 배치가 마음에 안 들 때 다시 굴리는 번호. 값만 바꾸면 다른 배치가 나온다.
	 *
	 * 기본 시드는 존 이름에서 나오므로 존마다 이미 다른 배치가 나온다. 이건 같은 존에서
	 * 배치만 다시 뽑고 싶을 때 쓴다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	int32 LayoutSeed = 0;

	// 패치를 격자 칸 안에서 얼마나 흔들 것인가. 0 이면 격자가 눈에 보이고, 1 이면 칸 끝까지 튄다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PatchJitter = 0.6f;

	/**
	 * 패치를 격자 칸보다 얼마나 크게 잡을 것인가.
	 *
	 * 1 이면 칸에 딱 맞아서 패치 사이가 벌어지고 먼지가 물방울무늬처럼 보인다.
	 * 머티리얼의 블롭 마스크가 가장자리를 둥글게 깎아내므로 넉넉히 겹쳐야 한 덩어리로 읽힌다.
	 * 1.35 로 시작했다가 실제로 물방울무늬가 나와서 1.7 로 올렸다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "1.0"))
	float PatchOverlap = 1.7f;

	// 패치 크기를 얼마나 들쭉날쭉하게 할 것인가. 가로세로에 따로 걸려서 블롭이 타원이 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float PatchScaleJitter = 0.25f;

	// 패치별 밝기를 얼마나 흔들 것인가. 0.1 을 넘기면 먼지가 아니라 얼룩으로 보이기 시작한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float TintJitter = 0.08f;

	/**
	 * 흙 텍스처 한 장을 월드에서 몇 cm 로 보이게 할 것인가. 흙 알갱이의 굵기다.
	 *
	 * 패치가 월드에서 차지하는 크기를 이 값으로 나눠 UV 배율을 만든다. 그래서 패치 크기나
	 * 존 스케일이 제각각이어도 알갱이 굵기는 어디서나 같다.
	 *
	 * 텍스처 원본은 2m x 2m 짜리다. 그대로 200 을 넣으면 알갱이가 굵어서 얼룩덜룩해 보인다.
	 * 기본값 70 은 원본을 3배 가깝게 조여 곱게 만든 값이다.
	 * 작을수록 곱고, 클수록 굵다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "1.0"))
	float DustTextureWorldSize = 70.f;

	/**
	 * 패치 하나가 걷히는 데 쓰는 구간의 폭. 패치 한 칸 분량을 1 로 본다.
	 *
	 * 작을수록 한 장씩 또렷하게 사라지고(단계감이 세다), 클수록 여러 장이 같이 옅어진다(부드럽다).
	 * 0 에 가까우면 그냥 툭툭 꺼지는 것으로 보이므로 너무 낮추지 않는 편이 좋다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "0.05", ClampMax = "4.0"))
	float PatchFadeWindow = 0.7f;

	/**
	 * 데칼이 위아래로 훑는 깊이의 절반(cm). DecalSize.X 로 들어간다.
	 *
	 * 존 원점 기준으로 위아래 이 값만큼이 투영 범위다. 도로면이 원점과 정확히 맞지 않아도
	 * 이 범위 안에 들어오면 먼지가 찍힌다. 반대로 너무 크게 잡으면 도로 옆 인도나 화단처럼
	 * 높이가 다른 표면까지 같이 더러워진다.
	 *
	 * 캐릭터나 차량에 씌워지는 걱정은 없다. 움직이는 것들은 데칼 수신을 껐다.
	 * (UCWDecalStatics 참고)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual", meta = (ClampMin = "1.0"))
	float DecalProjectionDepth = 32.f;

	/**
	 * 바닥 데칼끼리의 층 순서. 값이 크면 위에 그려진다.
	 *
	 * 먼지는 도로에 얹힌 것 중 가장 아래다. 페인트 자국(기본 0)과 싱크홀 균열(ACWSinkhole
	 * 에서 지정)이 먼지 위로 올라와야 한다. 먼지가 균열을 덮으면 이미 갈라진 바닥이
	 * 멀쩡해 보인다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dust|Visual")
	int32 DecalSortOrder = -10;

private:
	UPROPERTY(Transient)
	TArray<FCWDustPatch> DustPatches;

	/**
	 * 첫 진행도 반영이 끝났는가.
	 *
	 * 시작부터 진행도가 올라가 있는 존은 처음 계산할 때 여러 패치가 한꺼번에 0 이 된다.
	 * 그때 걷힘 연출을 그대로 태우면 레벨을 켜자마자 흙먼지가 무더기로 터진다.
	 */
	bool bPatchStateInitialized = false;
};
