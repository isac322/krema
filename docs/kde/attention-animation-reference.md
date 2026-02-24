# Attention-Demanding Animations Reference

Krema M7 기능 구현을 위한 레퍼런스 문서.
3가지 하위 기능: Icon Wiggle, Badge Count, Highlight Glow.

---

## 1. 데이터 소스

### 1.1 IsDemandingAttention (LibTaskManager)

```
// /usr/include/taskmanager/abstracttasksmodel.h:75
IsDemandingAttention, /**< Task is demanding attention. */
```

- 창이 attention을 요청할 때 true (예: 메신저 새 메시지, 백그라운드 작업 완료)
- KWin이 EWMH `_NET_WM_STATE_DEMANDS_ATTENTION` 상태로부터 전달
- `model.IsDemandingAttention`으로 QML에서 직접 접근 가능 (이미 DockModel에서 노출)

### 1.2 SmartLauncherItem (Badge/Progress/Urgent)

```
// /usr/lib/qt6/qml/org/kde/plasma/private/taskmanager/taskmanagerplugin.qmltypes
Component {
    name: "SmartLauncher::Item"
    exports: ["org.kde.plasma.private.taskmanager/SmartLauncherItem 254.0"]

    Property { name: "count";           type: "int";  isReadonly: true }
    Property { name: "countVisible";    type: "bool"; isReadonly: true }
    Property { name: "progress";        type: "int";  isReadonly: true }
    Property { name: "progressVisible"; type: "bool"; isReadonly: true }
    Property { name: "urgent";          type: "bool"; isReadonly: true }
}
```

- `count`/`countVisible`: 앱이 보고한 배지 숫자 (예: 읽지 않은 메시지 3개)
- `progress`/`progressVisible`: 0-100 진행률 (예: 다운로드 62%)
- `urgent`: 긴급 상태 (IsDemandingAttention과 별개의 D-Bus 기반 경로)
- 데이터 소스: Unity Launcher API (`com.canonical.Unity.LauncherEntry` D-Bus), KStatusNotifierItem

### 1.3 Attention 트리거 조건 (Plasma 참조)

```qml
// /usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/Task.qml:654-661
State {
    name: "attention"
    when: model.IsDemandingAttention || (task.smartLauncherItem && task.smartLauncherItem.urgent)
}
```

Plasma는 두 소스를 OR로 결합: 윈도우 매니저(IsDemandingAttention) + D-Bus(urgent).

---

## 2. Attention Animation 방식 비교

### 2.1 비교 표

| 방식 | 사용처 | 시각적 효과 | 장점 | 단점 |
|------|--------|------------|------|------|
| **Bounce (수직 바운스)** | macOS Dock, Latte Dock | 아이콘이 위아래로 반복 튕김 | 매우 눈에 띔, 직관적 | 과할 수 있음, 사용자에 따라 짜증 유발 |
| **Wiggle (좌우 흔들기)** | 일부 커스텀 독 | 아이콘이 좌우로 빠르게 흔들림 | Bounce보다 덜 침습적 | 독 방향(수직/수평)에 따라 부자연스러울 수 있음 |
| **Pulse (크기 맥동)** | Dash-to-Dock | 아이콘이 커졌다 작아졌다 반복 | 부드럽고 우아함 | 주변 아이콘 레이아웃에 영향 줄 수 있음 |
| **Color Glow (발광)** | Plank (urgent glow) | 아이콘 뒤에 빛나는 후광 | 레이아웃 영향 없음, 우아함 | 어두운 테마에서만 효과적 |
| **SVG State Change** | KDE Plasma Task Manager | 프레임 배경을 "attention" SVG로 변경 | 테마 일관성 완벽 | Krema는 패널이 아니라 독이라 SVG 테마 미사용 |
| **Indicator Dot Animation** | Latte Dock indicators | 인디케이터 점이 색 변경/깜빡임 | 미니멀, 기존 UI 활용 | 눈에 잘 안 띌 수 있음 |
| **Opacity Blink (깜빡임)** | 커스텀 구현 | 아이콘 투명도가 반복 변화 | 구현 매우 간단 | 시각적으로 불쾌할 수 있음 |

### 2.2 각 방식 상세 설명

#### A. Bounce (수직 바운스)

**시각적 예시**:
- [macOS Dock Animation 재현 (인터랙티브)](https://joeylene.com/labs/2025/mac-dock-animation) — 클릭 시 바운스 동작 확인 가능
- [CSS로 구현한 macOS Dock](https://www.frontend.fyi/tutorials/macos-dock-hover-animation-with-css) — 단계별 구현 + 라이브 데모
- [Latte Dock GitHub (스크린샷/영상)](https://github.com/KDE/latte-dock) — KDE 환경에서의 바운스 구현

**동작**: 아이콘이 독 표면에서 위로(하단 독 기준) 튀어올랐다 내려오기를 반복.

```
시간 →
높이  ╭─╮     ╭─╮     ╭─╮
  ↑  │  │    │  │    │  │
     ╯  ╰────╯  ╰────╯  ╰──── (attention 해제 시 마지막 사이클 완료 후 정지)
```

**macOS 구현**: 2가지 모드
- Launch bounce: 앱 실행 시 최대 30초 또는 앱 활성화까지 반복 (높이 ~아이콘 크기의 1/3)
- Alert bounce: attention 요청 시 3회 바운스 후 정지 (사용자가 앱 활성화할 때까지 재트리거 가능)

**Latte Dock 구현**: `inAttention` 속성에 따라 반복 바운스. macOS보다 부드러운 easing 사용.

**Krema 현재 상태**: 이미 launch bounce가 구현되어 있음 (DockItem.qml:338-381).
attention bounce를 추가할 때 launch bounce와 **다른 시각적 특성**을 줘야 구분 가능.

**QML 구현 패턴**:
```qml
SequentialAnimation {
    id: attentionBounce
    loops: Animation.Infinite
    NumberAnimation { target: bounceTranslate; property: "y"; to: -12; duration: 300; easing.type: Easing.OutQuad }
    NumberAnimation { target: bounceTranslate; property: "y"; to: 0; duration: 300; easing.type: Easing.InBounce }
    PauseAnimation { duration: 800 }  // 바운스 간 휴식
}
```

**주의**: Krema의 launch bounce와 충돌 방지 필요. 동시에 두 애니메이션이 활성화되면 안 됨.

#### B. Wiggle (좌우 흔들기)

**시각적 예시**:
- [Icon Wiggle Animation (CodePen)](https://codepen.io/jaflo/pen/DLwLeb) — iOS 스타일 아이콘 흔들림
- [CSS Shake Animation 변형 모음 (CodePen)](https://codepen.io/TClement/pen/ZEVPgor) — tilt-shake, vertical, horizontal, jump, skew 등 다양한 변형
- [CSShake 라이브러리 데모](https://elrumordelaluz.github.io/csshake/) — 강도별 흔들림 비교 (slow, little, hard, fixed, crazy 등)

**동작**: 아이콘이 좌우로 빠르게 흔들리며, 진폭이 점차 감소.

```
시간 →
각도  ╭╮ ╭╮ ╭╮ ╭╮
      ││╭╯│╭╯│╭╯│
  0 ──╯╰╯ ╰╯ ╰╯ ╰──── (감쇠 진동)
```

**iOS 영향**: iOS 홈 화면 편집 모드의 아이콘 흔들림에서 영감. 하지만 attention에는 과도할 수 있음.

**QML 구현 패턴**:
```qml
SequentialAnimation {
    id: wiggleAnim
    loops: 3  // 3사이클 후 정지
    NumberAnimation { target: iconRotation; property: "angle"; to: 5; duration: 60; easing.type: Easing.InOutSine }
    NumberAnimation { target: iconRotation; property: "angle"; to: -5; duration: 120; easing.type: Easing.InOutSine }
    NumberAnimation { target: iconRotation; property: "angle"; to: 0; duration: 60; easing.type: Easing.InOutSine }
    PauseAnimation { duration: 2000 }  // 반복 전 대기
}
// Transform: Rotation { id: iconRotation; origin.x: width/2; origin.y: height/2 }
```

**성능**: Rotation은 GPU 가속됨. CPU 부하 거의 없음.

#### C. Pulse (크기 맥동)

**시각적 예시**:
- [CSS Pulse Animation 가이드 (GeeksforGeeks)](https://www.geeksforgeeks.org/css/css-pulse-animation/) — 기본 pulse 동작 라이브 데모
- [Pulse Animation CSS 모음 (TemplateFlip)](https://templateflip.com/blog/pulse-animation-css/) — pulse, ring, circle 변형 비교
- [CSS Pulse Effect (Florin Pop)](https://www.florin-pop.com/blog/2019/03/css-pulse-effect/) — 단계별 구현 + 라이브 결과

**동작**: 아이콘이 1.0 → 1.15 → 1.0 스케일을 부드럽게 반복.

```
시간 →
크기  ╭──╮    ╭──╮    ╭──╮
 1.15│    │  │    │  │    │
 1.0 ╯    ╰──╯    ╰──╯    ╰──
```

**QML 구현 패턴**:
```qml
SequentialAnimation on attentionScale {
    loops: Animation.Infinite
    NumberAnimation { to: 1.15; duration: 600; easing.type: Easing.InOutSine }
    NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
}
```

**주의**: Krema의 parabolic zoom과 상호작용 고려 필요. `currentScale`에 곱하는 방식이면 zoom + pulse가 동시 적용됨.

#### D. Color Glow (발광 후광)

**시각적 예시**:
- [62 CSS Glow Effects 모음 (FreeFrontend)](https://freefrontend.com/css-glow-effects/) — 다양한 glow 스타일 라이브 데모
- [30+ CSS Glow Effects (DevSnap)](https://devsnap.me/css-glow-effects/) — neon, pulsing, hover glow 등 비교
- [Neon Glow 만들기 (CSS-Tricks)](https://css-tricks.com/how-to-create-neon-text-with-css/) — box-shadow 기반 glow 기법 상세
- [Blinking Neon Sign (CodePen)](https://codepen.io/DevchamploO/pen/NBWBGq) — 맥동하는 네온 효과

**동작**: 아이콘 뒤에 색상 있는 글로우가 나타나며 밝기가 맥동.

```
          ░░░░░░░
        ░░  ████  ░░
       ░░  ██  ██  ░░     (░ = glow, █ = icon)
        ░░  ████  ░░
          ░░░░░░░
```

**Plank 구현**: urgent 상태에서 아이콘 뒤에 반투명 원형 글로우 표시. 기본적으로 glow가 bounce보다 선호됨.

**QML 구현 옵션 3가지** (아래 섹션 3에서 상세 비교):
1. `MultiEffect` (Qt 6.5+) shadow/glow 사용
2. `Rectangle` + `RadialGradient` + opacity 애니메이션
3. 커스텀 `ShaderEffect`

#### E. Indicator Dot Color Change

**동작**: 기존 인디케이터 점의 색상이 accent color/주황색으로 변경되고 깜빡임.

```
기본:    ● ● ●  (Theme.textColor)
주의:    ◉ ◉ ◉  (Theme.negativeTextColor, 깜빡임)
```

**QML 구현 패턴**:
```qml
Rectangle {
    color: model.IsDemandingAttention ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor
    Behavior on color { ColorAnimation { duration: Kirigami.Units.longDuration } }
    // 또는 opacity 깜빡임
    SequentialAnimation on opacity {
        running: model.IsDemandingAttention
        loops: Animation.Infinite
        NumberAnimation { to: 0.3; duration: 500 }
        NumberAnimation { to: 1.0; duration: 500 }
    }
}
```

### 2.3 Krema 권장 조합

Launch bounce가 이미 있으므로, attention은 **시각적으로 명확히 구분**되어야 함.

| 조합 | 설명 | 복잡도 |
|------|------|--------|
| **Wiggle + Dot Color** | 아이콘 흔들림 + 인디케이터 색 변경 | 낮음 |
| **Bounce (다른 패턴) + Dot Color** | launch와 다른 리듬의 바운스 + 인디케이터 | 중간 |
| **Glow + Dot Color** | 후광 + 인디케이터 색 변경 | 중간~높음 |
| **Pulse + Glow** | 크기 맥동 + 후광 | 높음 |

---

## 3. Highlight Glow 구현 옵션 비교

### 3.1 비교 표

| 방식 | GPU 가속 | 동적 크기 | 테마 통합 | 복잡도 | 성능 |
|------|---------|----------|----------|--------|------|
| **MultiEffect shadow** | O (QRhi) | O | 수동 색상 설정 | 낮음 | 좋음 (단, blur 비용) |
| **Rectangle + Gradient** | O | O | Kirigami.Theme 직접 사용 | 매우 낮음 | 매우 좋음 |
| **ShaderEffect 커스텀** | O (QRhi) | O | 수동 | 높음 | 좋음 |
| **KGraphicalEffects.BadgeEffect** | O | 배지 전용 | Plasma 테마 | 중간 | 좋음 |

### 3.2 각 방식 상세

#### A. MultiEffect (Qt 6.5+)

**참고 링크**:
- [MultiEffect 공식 문서 (Qt 6)](https://doc.qt.io/qt-6/qml-qtquick-effects-multieffect.html) — 모든 속성 설명 + 시각적 예시
- [MultiEffect Test Bed 예제 (Qt)](https://doc.qt.io/qt-6/qtquick-multieffect-testbed-example.html) — 인터랙티브 파라미터 조정 데모 (Qt Creator에서 실행)
- [Qt Graphical Effects → MultiEffect 마이그레이션](https://ekkesapps.wordpress.com/qt-6-qmake/from-qt-graphical-effects-to-multi-effect/) — 전후 비교

```qml
import QtQuick.Effects

MultiEffect {
    source: iconImage
    anchors.fill: iconImage
    shadowEnabled: true
    shadowColor: Kirigami.Theme.highlightColor
    shadowBlur: 0.8        // 0.0-1.0, 높을수록 부드러운 글로우
    shadowOpacity: attentionGlowOpacity  // 애니메이션 대상
    shadowScale: 1.1       // 아이콘보다 약간 크게
    shadowHorizontalOffset: 0
    shadowVerticalOffset: 0
}
```

- **장점**: Qt 공식 API, 단일 셰이더에서 blur+shadow+colorize 통합, QRhi 하드웨어 가속
- **단점**: blur가 포함되면 매 프레임 GPU 작업 증가. `blurMultiplier` 대신 `blurMax`를 줄여서 최적화 권장
- **성능 팁**: `shadowEnabled`만 사용하고 다른 효과(blur, colorize)는 꺼두면 가벼움
- **참조**: https://doc.qt.io/qt-6/qml-qtquick-effects-multieffect.html

#### B. Rectangle + RadialGradient (가장 가벼움)

```qml
Rectangle {
    id: attentionGlow
    anchors.centerIn: iconImage
    width: iconImage.width * 1.6
    height: width
    radius: width / 2
    visible: model.IsDemandingAttention
    opacity: 0

    gradient: RadialGradient {
        centerX: attentionGlow.width / 2
        centerY: attentionGlow.height / 2
        centerRadius: attentionGlow.width / 2
        focalX: centerX; focalY: centerY
        GradientStop { position: 0.0; color: Qt.alpha(Kirigami.Theme.highlightColor, 0.4) }
        GradientStop { position: 0.5; color: Qt.alpha(Kirigami.Theme.highlightColor, 0.15) }
        GradientStop { position: 1.0; color: "transparent" }
    }

    SequentialAnimation on opacity {
        running: attentionGlow.visible
        loops: Animation.Infinite
        NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutSine }
        NumberAnimation { to: 0.3; duration: 800; easing.type: Easing.InOutSine }
    }
}
```

- **장점**: 셰이더 컴파일 불필요, GPU 부하 최소, RadialGradient는 GPU에서 직접 렌더
- **단점**: 정교한 블러 효과 불가 (그라데이션 경계가 선명)
- **적합 상황**: 60fps 유지가 중요하고 글로우가 단순 후광이면 충분한 경우

#### C. 커스텀 ShaderEffect

```qml
ShaderEffect {
    anchors.centerIn: iconImage
    width: iconImage.width * 1.6
    height: width
    property color glowColor: Kirigami.Theme.highlightColor
    property real intensity: attentionGlowIntensity  // 0.0-1.0 애니메이션
    fragmentShader: "qrc:/shaders/glow.frag.qsb"
}
```

- **장점**: 완전한 시각적 제어, 가우시안 블러 등 정교한 효과 가능
- **단점**: GLSL→QSB 컴파일 필요, 유지보수 부담, Vulkan/OpenGL 호환성 관리
- **적합 상황**: 매우 특별한 시각적 효과가 필요한 경우에만

### 3.3 권장

**Rectangle + Gradient**를 기본으로 시작. 시각적으로 부족하면 **MultiEffect shadow**로 업그레이드.
커스텀 ShaderEffect는 필요성이 입증될 때까지 사용하지 않음 (YAGNI).

---

## 4. Badge Count 구현 옵션 비교

### 4.1 SmartLauncherItem 접근 경로

| 방식 | 접근성 | 장점 | 단점 |
|------|--------|------|------|
| **QML에서 직접 사용** | `org.kde.plasma.private.taskmanager` import | 코드 최소, Plasma과 동일 | private API 의존, 안정성 보장 없음 |
| **C++ 래핑** | DockModel에서 SmartLauncher 래핑하여 role 추가 | 안정적 API, 캐싱 가능 | 구현 복잡도 높음 |
| **D-Bus 직접 구현** | `com.canonical.Unity.LauncherEntry` 모니터링 | 완전한 제어 | 많은 보일러플레이트 |

### 4.2 Plasma Task Manager의 SmartLauncherItem 사용 패턴

```qml
// /usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/Task.qml:237-246
onSmartLauncherEnabledChanged: {
    if (smartLauncherEnabled && !smartLauncherItem) {
        const component = Qt.createComponent("org.kde.plasma.private.taskmanager", "SmartLauncherItem");
        const smartLauncher = component.createObject(task);
        component.destroy();
        smartLauncher.launcherUrl = Qt.binding(() => model.LauncherUrlWithoutIcon);
        smartLauncherItem = smartLauncher;
    }
}
```

- `LauncherUrlWithoutIcon` role로 앱의 .desktop 파일 경로를 SmartLauncherItem에 바인딩
- SmartLauncherItem이 D-Bus를 통해 해당 앱의 count/progress/urgent를 자동 추적

### 4.3 Badge UI 렌더링 (Plasma 참조)

**레이어 구조** (TaskBadgeOverlay.qml):

```
1. badgeMask (Rectangle) — 배지 모양의 마스크
2. iconShaderSource — 원본 아이콘을 ShaderEffectSource로 캡처
3. maskShaderSource — 마스크를 ShaderEffectSource로 캡처
4. KGraphicalEffects.BadgeEffect — 아이콘에서 배지 영역을 잘라내는 셰이더
5. Badge (Rectangle) — 실제 배지 UI (숫자 표시)
```

**Badge.qml 핵심 로직**:
```qml
Rectangle {
    radius: height / 2  // 원형
    color: Kirigami.Theme.backgroundColor
    Rectangle {  // 내부 색상 배경
        color: Qt.alpha(Kirigami.Theme.highlightColor, 0.3)
        border.color: Kirigami.Theme.highlightColor
    }
    PlasmaComponents3.Label {
        text: number > 9999 ? "9,999+" : number.toLocaleString(Qt.locale(), 'f', 0)
        fontSizeMode: Text.VerticalFit  // 크기 자동 맞춤
    }
}
```

### 4.4 Badge UI 디자인 레퍼런스

- [Material Design 3 — Badge 가이드라인](https://m3.material.io/components/badges/guidelines) — 배지 크기, 위치, 숫자 표시 규칙
- [Ant Design — Badge 컴포넌트](https://ant.design/components/badge/) — 인터랙티브 데모 (overflow, 애니메이션, 상태 표시)
- [Notification Badge 디자인 영감 (Dribbble)](https://dribbble.com/tags/notification-badge) — 다양한 배지 디자인 시각적 예시
- [KDE Discuss — Task Manager 배지 지원 논의](https://discuss.kde.org/t/support-notification-badge-numbers-on-icons-in-icons-only-task-manager/33593) — KDE에서의 배지 표시 현황 + 스크린샷

### 4.5 Krema 구현 옵션 비교

| 방식 | 복잡도 | 시각적 품질 | 의존성 |
|------|--------|------------|--------|
| **A. Plasma 패턴 그대로** (SmartLauncherItem QML + BadgeEffect) | 중간 | 높음 (아이콘 잘라내기) | private API + KGraphicalEffects |
| **B. SmartLauncherItem QML + 단순 Badge** (BadgeEffect 없이) | 낮음 | 중간 (배지가 아이콘 위에 겹침) | private API만 |
| **C. C++ SmartLauncher 래핑 + 단순 Badge** | 중간~높음 | 중간 | 없음 (자체 구현) |

**방식 B 예시** (BadgeEffect 없이 간단한 오버레이):
```qml
// DockItem.qml에 추가
Rectangle {
    id: badge
    visible: smartLauncherItem && smartLauncherItem.countVisible
    anchors.right: iconImage.right
    anchors.top: iconImage.top
    anchors.rightMargin: -width * 0.25
    anchors.topMargin: -height * 0.25
    width: Math.max(height, badgeLabel.contentWidth + Kirigami.Units.smallSpacing * 2)
    height: Kirigami.Units.gridUnit
    radius: height / 2
    color: Kirigami.Theme.highlightColor

    QQC2.Label {
        id: badgeLabel
        anchors.centerIn: parent
        text: smartLauncherItem.count > 9999 ? "9999+" : smartLauncherItem.count
        color: Kirigami.Theme.highlightedTextColor
        font.pixelSize: parent.height * 0.7
        font.bold: true
    }
}
```

### 4.5 권장

**방식 B (SmartLauncherItem + 단순 Badge)**로 시작.
- private API이지만 Plasma 6 동안 안정적이고, Plasma 자체가 사용하는 API
- BadgeEffect (아이콘 잘라내기)는 시각적으로 좋지만 필수는 아님
- 이후 필요하면 BadgeEffect 추가 또는 C++ 래핑으로 마이그레이션

---

## 5. 참조 소스 경로

### KDE 시스템 헤더/파일
| 파일 | 내용 |
|------|------|
| `/usr/include/taskmanager/abstracttasksmodel.h` | IsDemandingAttention role 정의 |
| `/usr/include/KF6/KWindowSystem/netwm_def.h` | DemandsAttention EWMH 상태 |
| `/usr/lib/qt6/qml/org/kde/plasma/private/taskmanager/taskmanagerplugin.qmltypes` | SmartLauncherItem API |
| `/usr/lib/qt6/qml/org/kde/graphicaleffects/BadgeEffect.qml` | 배지 셰이더 래퍼 |

### Plasma Task Manager 참조 구현
| 파일 | 내용 |
|------|------|
| `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/Task.qml` | attention 상태 + SmartLauncherItem 초기화 |
| `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/TaskBadgeOverlay.qml` | 배지 마스킹 + ShaderEffectSource |
| `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/Badge.qml` | 배지 숫자 UI |
| `/usr/share/plasma/plasmoids/org.kde.plasma.taskmanager/contents/ui/TaskProgressOverlay.qml` | 진행률 바 |

### Krema 현재 코드
| 파일 | 내용 |
|------|------|
| `src/qml/DockItem.qml` | launch bounce (재사용 가능), 인디케이터 점 |
| `src/models/taskiconprovider.h` | 아이콘 제공 (badge 오버레이 시 참조) |
