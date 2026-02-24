# Icon Size Normalization Reference

Krema M7 기능 구현을 위한 레퍼런스 문서.
아이콘 내부 투명 패딩을 감지하여 시각적 크기를 균일하게 맞추는 기능.

---

## 1. 문제 정의

### 1.1 왜 필요한가

Linux 아이콘 테마에서 앱 아이콘의 **시각적 크기가 일정하지 않음**:

```
   ┌────────┐  ┌────────┐  ┌────────┐
   │ ██████ │  │        │  │ ██████ │
   │ ██████ │  │  ████  │  │ █    █ │
   │ ██████ │  │  ████  │  │ █    █ │
   │ ██████ │  │        │  │ ██████ │
   └────────┘  └────────┘  └────────┘
    Firefox     Terminal     FileManager
   (거의 꽉 참) (큰 패딩)   (테두리형)
```

48x48 아이콘이라도:
- Firefox: 실제 콘텐츠 46x46, 패딩 1px
- Terminal: 실제 콘텐츠 32x32, 패딩 8px
- 일부 앱: 실제 콘텐츠가 아이콘 크기의 60%만 차지

독에서 나란히 놓으면 **크기 차이가 눈에 띔**.

### 1.2 freedesktop Icon Theme Specification

- 아이콘 크기: 16, 22, 24, 32, 48, 64, 128, 256 등 고정 크기 디렉토리
- 패딩에 대한 규정: **없음** (각 아이콘 디자이너 재량)
- Symbolic 아이콘: `-symbolic` 접미사, 단색, 패딩이 상대적으로 일정
- 풀컬러 앱 아이콘: 패딩 양이 아이콘마다 크게 다름

### 1.3 Krema 현재 구현

```cpp
// src/models/taskiconprovider.h:28-44
QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override {
    QIcon icon = QIcon::fromTheme(id);
    QPixmap pixmap = icon.pixmap(pixmapSize);  // 그대로 반환, 패딩 처리 없음
    return pixmap;
}
```

```qml
// src/qml/DockItem.qml:263-282
Image {
    width: iconSize      // 모든 아이콘 동일 크기
    height: iconSize
    sourceSize: Qt.size(Math.ceil(iconSize * maxZoomFactor), ...)
    source: "image://icon/" + name
}
```

---

## 2. 시각적 레퍼런스

정규화의 전후 차이와 관련 개념을 확인할 수 있는 링크:

- [아이콘/로고 시각적 비율 자동 정규화 (Dan Paquette)](https://danpaquette.net/read/automatically-resizing-a-list-of-icons-or-logos-so-theyre-visually-proportional/) — "Proportional Image Normalization Formula" 개념 설명 + 전후 비교 이미지
- [Online PNG Tool — Remove Space Around Icon](https://onlinepngtools.com/remove-space-around-icon) — 투명 패딩 제거 전후를 브라우저에서 직접 확인 가능
- [Online PNG Tool — Remove Padding from PNG](https://onlinepngtools.com/remove-padding-from-png) — 각 변의 패딩 제거량 조절하며 결과 확인
- [Trim Transparent Pixels Online (ImageOnline.io)](https://imageonline.io/trim-transparent/) — 투명 영역 트리밍 전후 비교
- [아이콘 시각적 크기 정렬 최적화 연구 (ScienceDirect)](https://www.sciencedirect.com/science/article/abs/pii/S0141938223002056) — 아이콘 세트 디자인에서의 시각적 크기/정렬 최적화 학술 연구
- [KDE HIG — Icons](https://develop.kde.org/hig/icons/) — KDE 아이콘 디자인 가이드라인, 패딩/크기 권장사항
- [Icon Paddings (Uxcel)](https://app.uxcel.com/courses/design-foundations/basics-457/paddings-5113) — 아이콘 패딩 디자인 원칙 시각적 설명

---

## 3. 알고리즘 비교

### 3.1 비교 표

| 알고리즘                        | 정확도   | 성능          | 복잡도 | SVG 지원 | 엣지 케이스    |
|-----------------------------|-------|-------------|-----|--------|-----------|
| **A. Alpha Bounding Box**   | 높음    | 빠름 (1회 스캔)  | 낮음  | O      | 반투명 테두리   |
| **B. Alpha Threshold Trim** | 매우 높음 | 빠름 (1회 스캔)  | 낮음  | O      | 임계값 선택    |
| **C. Content-Aware Scale**  | 최고    | 느림 (2회 스캔)  | 중간  | O      | 과도한 패딩    |
| **D. Fixed Ratio Padding**  | 낮음    | 없음 (계산 불필요) | 최저  | O      | 부정확       |
| **E. Icon Metadata 기반**     | 테마 의존 | 없음 (메타 읽기)  | 낮음  | 일부     | 메타 없으면 무용 |

### 3.2 각 알고리즘 상세

#### A. Alpha Bounding Box (기본)

**원리**: 픽셀의 alpha 값이 0이 아닌 영역의 bounding box를 찾아서, 그 영역만 사용.

```
원본 48x48:          분석 결과:
┌────────────────┐    contentRect = QRect(8, 4, 32, 40)
│    ░░░░░░░░    │    → 실제 콘텐츠: 32x40
│  ░░████████░░  │    → 패딩: 상4, 하4, 좌8, 우8
│  ░░████████░░  │
│  ░░████████░░  │    정규화: 40/48 = 0.833 → scale factor
│    ░░░░░░░░    │    → 아이콘을 48/40 = 1.2배 확대하고
└────────────────┘      중앙 40x40 영역을 crop
```

**C++ 구현**:
```cpp
QRect findContentBounds(const QImage &image) {
    int top = image.height(), left = image.width();
    int bottom = 0, right = 0;

    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(line[x]) > 0) {
                top = std::min(top, y);
                bottom = std::max(bottom, y);
                left = std::min(left, x);
                right = std::max(right, x);
            }
        }
    }

    if (bottom < top) return {};  // 완전 투명
    return QRect(left, top, right - left + 1, bottom - top + 1);
}
```

- **장점**: 간단하고 정확, 1회 풀스캔으로 완료
- **단점**: alpha=1인 안티앨리어싱 아티팩트가 경계를 늘릴 수 있음
- **성능**: 48x48 = 2304 픽셀 → μs 단위, 128x128 = 16384 픽셀 → 여전히 μs 단위

#### B. Alpha Threshold Trim (권장)

**원리**: A와 동일하지만, alpha > **threshold** (예: 25)인 픽셀만 콘텐츠로 간주.
안티앨리어싱 아티팩트와 극히 미미한 그림자를 무시.

```cpp
QRect findContentBounds(const QImage &image, int alphaThreshold = 25) {
    // ... A와 동일하되 조건만 변경:
    if (qAlpha(line[x]) > alphaThreshold) { ... }
}
```

- **장점**: 안티앨리어싱과 미세 그림자를 무시하여 더 정확한 경계
- **단점**: threshold 값 선택이 주관적 (25가 일반적으로 잘 작동)
- **QGIS 참조**: `QgsImageOperation::nonTransparentImageRect()` 가 유사한 접근법 사용

#### C. Content-Aware Scale (고급)

**원리**: 2단계 접근.
1. Alpha Threshold로 content bounds 계산
2. content가 차지하는 비율이 threshold 이하 (예: 75%)이면 → 확대 + 중앙 crop

```
원본:                    정규화 후:
┌──────────────┐         ┌──────────────┐
│              │         │  ████████    │
│    ████      │    →    │  ████████    │
│    ████      │         │  ████████    │
│              │         │  ████████    │
└──────────────┘         └──────────────┘
콘텐츠 비율: 45%          콘텐츠 비율: 85%
```

**추가 규칙**:
- 확대 비율 상한 설정 (예: 최대 1.4배) → 과도한 확대 방지
- 정사각형이 아닌 콘텐츠는 가로/세로 중 큰 쪽 기준으로 비율 유지
- 최소 마진 보장 (예: 아이콘 크기의 4%) → 아이콘이 완전히 가장자리에 붙지 않게

```cpp
struct NormalizationResult {
    qreal scaleFactor;    // 1.0 = 변경 없음, >1.0 = 확대
    QPointF offset;       // 확대 후 중앙 정렬 오프셋
    QRect contentBounds;  // 원본에서의 콘텐츠 영역
};

NormalizationResult calculateNormalization(const QImage &image,
                                           qreal maxScaleFactor = 1.4,
                                           qreal minContentRatio = 0.75,
                                           qreal minMarginRatio = 0.04) {
    auto bounds = findContentBounds(image, 25);
    if (bounds.isEmpty()) return {1.0, {0,0}, bounds};

    qreal contentRatio = qreal(qMax(bounds.width(), bounds.height())) / image.width();
    if (contentRatio >= minContentRatio) return {1.0, {0,0}, bounds};  // 이미 충분히 큼

    qreal targetRatio = minContentRatio;
    qreal scale = targetRatio / contentRatio;
    scale = qMin(scale, maxScaleFactor);

    // 마진 보장
    qreal effectiveContentSize = contentRatio * scale;
    if (effectiveContentSize > (1.0 - 2 * minMarginRatio)) {
        scale = (1.0 - 2 * minMarginRatio) / contentRatio;
    }

    // 오프셋: 확대 후 콘텐츠 중심이 아이콘 중심에 오도록
    QPointF contentCenter(bounds.center());
    QPointF imageCenter(image.width() / 2.0, image.height() / 2.0);
    QPointF offset = imageCenter - contentCenter * scale;

    return {scale, offset, bounds};
}
```

- **장점**: 가장 정교한 결과, 과도한 확대 방지
- **단점**: 더 복잡한 로직, 파라미터 튜닝 필요

#### D. Fixed Ratio Padding (가장 단순)

**원리**: 모든 아이콘에 고정 비율 패딩 제거 (예: 각 변에서 10% 제거).

```cpp
// 아이콘의 80% 영역만 사용 (10% 마진 제거)
qreal margin = pixmapSize * 0.1;
QRect cropRect(margin, margin, pixmapSize - 2*margin, pixmapSize - 2*margin);
QPixmap cropped = pixmap.copy(cropRect).scaled(pixmapSize, pixmapSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
```

- **장점**: 구현 1줄, 성능 영향 없음
- **단점**: 모든 아이콘에 동일하게 적용 → 이미 패딩 없는 아이콘의 가장자리가 잘림
- **적합 상황**: 특정 아이콘 테마에서만 사용하거나, 사용자가 수동으로 비율 조정할 때

#### E. Icon Metadata 기반

**원리**: SVG 아이콘의 metadata에서 콘텐츠 영역 정보 추출, 또는 아이콘 테마의 `index.theme`에서 패딩 정보 확인.

- freedesktop Icon Theme Spec에는 패딩 메타데이터가 **없음**
- 일부 SVG 아이콘은 `viewBox` 속성으로 콘텐츠 영역을 정의하지만, 표준이 아님
- **실용적이지 않음** → 대부분의 아이콘에 메타데이터 없음

---

## 4. 적용 위치

### 4.1 TaskIconProvider (C++) vs QML

| 적용 위치 | 장점 | 단점 |
|-----------|------|------|
| **TaskIconProvider::requestPixmap()** | 캐시 1회, QML 코드 변경 불필요 | 이미지 프로바이더가 동기적이라 첫 로딩 지연 |
| **QML Image + sourceClipRect** (Qt 6.2+) | C++ 변경 불필요 | 클립만 가능, 확대 불가 |
| **별도 NormalizationCache (C++)** | 비동기 계산 가능, 결과 캐시 | 추가 클래스 필요 |

### 4.2 권장: TaskIconProvider 확장

```cpp
// taskiconprovider.h — 개념적 변경
class TaskIconProvider : public QQuickImageProvider {
public:
    void setNormalizationEnabled(bool enabled);

    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override {
        // 기존 아이콘 로딩...
        QPixmap pixmap = icon.pixmap(pixmapSize);

        if (m_normalizationEnabled) {
            pixmap = normalizeIcon(pixmap, pixmapSize);
        }

        return pixmap;
    }

private:
    QPixmap normalizeIcon(const QPixmap &source, const QSize &targetSize);
    QHash<QString, NormalizationResult> m_cache;  // 아이콘 이름별 캐시
    bool m_normalizationEnabled = true;
};
```

### 4.3 캐싱 전략

| 전략 | 메모리 | 첫 로딩 | 반복 로딩 |
|------|--------|---------|----------|
| **결과 캐시 없음** | 0 | 느림 | 느림 |
| **NormalizationResult 캐시** (scaleFactor + offset만) | ~수 KB | 느림 | 빠름 (스캔 생략) |
| **정규화된 Pixmap 캐시** | ~수 MB | 느림 | 가장 빠름 |

**권장**: NormalizationResult 캐시.
- QML의 Image는 이미 pixmap을 캐시하므로, requestPixmap()이 매번 호출되지 않음
- 하지만 sourceSize가 변할 때 (zoom) 재요청됨 → scaleFactor만 캐시하면 재계산 불필요

---

## 5. 엣지 케이스

### 5.1 아이콘 유형별 동작

| 아이콘 유형 | 패딩 특성 | 정규화 동작 |
|------------|----------|-------------|
| **풀컬러 앱 아이콘** (Firefox, Dolphin 등) | 다양 (0-30%) | 정규화 적용 |
| **Symbolic 아이콘** (시스템 아이콘) | 일정 (~20%) | 독에서 거의 사용 안 됨 |
| **래스터 전용** (.png만 있는 앱) | 예측 불가 | 정규화 적용 |
| **SVG 아이콘** | scalable, 패딩 다양 | 래스터화 후 정규화 |
| **완전 투명 아이콘** (아이콘 없음) | 100% 투명 | 정규화 건너뜀 (fallback 사용) |
| **거의 꽉 찬 아이콘** (패딩 <5%) | 정규화 불필요 | scaleFactor ≈ 1.0 → 사실상 무변경 |

### 5.2 주의 사항

1. **과도한 확대 방지**: scaleFactor 상한 (1.3-1.5) 필수. 아이콘이 32x32 콘텐츠인 128x128 아이콘을 4배 확대하면 깨짐
2. **비정사각형 콘텐츠**: 세로로 긴 아이콘 (예: 연필 형태)은 가로로 확대해도 시각적으로 부자연스러움 → aspect ratio 유지
3. **반투명 배경이 있는 아이콘**: 일부 아이콘은 완전 투명이 아닌 반투명 배경 사용 → threshold가 너무 낮으면 패딩 감지 실패
4. **아이콘 테마 변경**: 테마 변경 시 캐시 무효화 필요
5. **HiDPI**: devicePixelRatio 고려. 96DPI에서 48px 아이콘은 실제 48px, 200% 스케일에서는 96px

---

## 6. 다른 구현체 참조

### 6.1 macOS

- macOS는 **앱 번들의 .icns 파일**에 정확한 크기 변형 포함
- 시스템이 아이콘 크기를 **강제 정규화하지 않음** — 디자인 가이드라인으로 일관성 확보
- Apple HIG: 앱 아이콘은 반드시 1024x1024 정사각형, 시스템이 자동 라운딩
- 패딩은 디자이너 책임이며, Apple 제공 템플릿이 균일한 크기 보장

### 6.2 Plank

- Plank은 아이콘 정규화 **미지원**
- 모든 아이콘을 요청 크기 그대로 렌더링
- 사용자가 수동으로 아이콘을 교체하여 크기 조정

### 6.3 Latte Dock

- Latte의 changelog에서 "icon padding" 관련 기능 언급 없음
- Latte는 parabolic zoom에 집중했으며, 아이콘 크기 정규화는 구현하지 않음

### 6.4 GNOME Dash-to-Dock

- GNOME은 `IconGrid`에서 아이콘 크기를 균일하게 배치하지만 패딩 정규화는 없음
- GNOME HIG는 앱 아이콘 디자이너에게 일관된 패딩을 권장하지만 강제하지 않음

### 6.5 Windows 11 Taskbar

- Windows는 아이콘을 시스템 크기 (보통 32x32 또는 48x48)로 강제 리사이즈
- 투명 패딩 정규화 없음 — 아이콘 디자인 가이드라인으로 해결

**결론**: 주요 dock/taskbar 중 **투명 패딩 자동 정규화를 구현한 곳은 없음**.
Krema가 이를 구현하면 **차별화 포인트**가 됨.

---

## 7. 설정 UI

```
[Icon Size Normalization]
(●) 자동 정규화 (권장)    — 아이콘 내부 패딩을 자동 감지하여 시각적 크기를 균일하게
( ) 원본 크기 그대로       — 아이콘 테마에서 제공하는 크기를 변경 없이 사용
```

KConfigXT 설정:
```xml
<entry name="IconNormalization" type="Bool">
    <label>Normalize icon sizes by removing internal padding</label>
    <default>true</default>
</entry>
```

---

## 8. 참조 소스 경로

### Krema 현재 코드
| 파일 | 내용 |
|------|------|
| `src/models/taskiconprovider.h` | 아이콘 로딩 (정규화 로직 추가 위치) |
| `src/qml/DockItem.qml:263-282` | Image 크기/소스 바인딩 |
| `src/config/krema.kcfg` | KConfigXT 설정 스키마 |

### Qt API
| API | 용도 |
|-----|------|
| `QImage::constScanLine(y)` | 라인 단위 픽셀 접근 (가장 빠름) |
| `qAlpha(QRgb)` | ARGB에서 alpha 추출 |
| `QImage::Format_ARGB32_Premultiplied` | alpha 포함 포맷 확인 |
| `QPixmap::toImage()` / `QPixmap::fromImage()` | Pixmap↔Image 변환 |
| `QImage::scaled()` | 확대/축소 (Qt::SmoothTransformation 권장) |
| `QImage::copy(QRect)` | 영역 crop |

### 외부 참조
| 소스 | 내용 |
|------|------|
| freedesktop Icon Theme Specification | 아이콘 크기 디렉토리 규칙, 패딩 규정 없음 |
| QGIS `QgsImageOperation::nonTransparentImageRect()` | 유사 알고리즘 참조 |
| KDE HIG Icons (develop.kde.org/hig/icons/) | KDE 아이콘 디자인 가이드라인 |
