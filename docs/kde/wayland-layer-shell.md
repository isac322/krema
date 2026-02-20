# Wayland Layer-Shell

> Krema 프로젝트에서 확인한 동작 기반

## 기본 동작

- Layer-shell 서피스는 `QWindow::geometry()`로 스크린 위치를 반환하지 않음
- 대신 스크린 geometry + edge + margin으로 서피스 위치를 직접 계산해야 함
- Krema에서 layer-shell margin은 항상 0

## 입력 영역 (Input Region)

- `DockPlatform::setInputRegion(QRegion)` — 서피스의 입력 수신 영역 설정
- 빈 `QRegion()`은 **전체 서피스**가 입력을 받음을 의미 (제한 없음)
- 독이 숨겨진 상태에서도 하단 trigger strip은 유지해야 마우스 호버 감지 가능

## 서피스 좌표 계산

Layer-shell 서피스의 스크린 좌표는 앵커된 edge에 따라 결정:

```cpp
const QRect screenGeo = dockWindow->screen()->geometry();
const int surfaceW = dockWindow->width();
const int surfaceH = dockWindow->height();

switch (edge) {
case Bottom:
    surfaceX = screenGeo.x();
    surfaceY = screenGeo.y() + screenGeo.height() - surfaceH;
    break;
case Top:
    surfaceX = screenGeo.x();
    surfaceY = screenGeo.y();
    break;
case Left:
    surfaceX = screenGeo.x();
    surfaceY = screenGeo.y();
    break;
case Right:
    surfaceX = screenGeo.x() + screenGeo.width() - surfaceW;
    surfaceY = screenGeo.y();
    break;
}

// 패널의 스크린 좌표 = 서피스 원점 + 패널의 로컬 오프셋
QRect panelScreenRect(surfaceX + panelX, surfaceY + panelY, panelW, panelH);
```
