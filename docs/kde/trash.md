# Trash (KIO + KCoreDirLister)

Verified against the KF6 KIO and KJobWidgets public headers
(`KIOCore/KCoreDirLister`, `KIOCore/CopyJob`, `KIOCore/EmptyTrashJob`,
`KIOGui/OpenUrlJob`, and `KJobWidgets/KNotificationJobUiDelegate`) used by
Krema. The implementation lives in `src/models/trashmanager.{h,cpp}`.

## Watching `trash:/`

`KCoreDirLister` is the signal-driven directory lister for the Trash URL:

```cpp
#include <KCoreDirLister>

auto *lister = new KCoreDirLister(parent);
connect(lister, &KCoreDirLister::newItems, receiver, [receiver](const auto &) {
    receiver->updateCount();
});
connect(lister, &KCoreDirLister::itemsAdded, receiver, [receiver](const auto &, const auto &) {
    receiver->updateCount();
});
connect(lister, &KCoreDirLister::itemsDeleted, receiver, [receiver](const auto &) {
    receiver->updateCount();
});
connect(lister, &KCoreDirLister::clear, receiver, &Receiver::updateCount);
connect(lister, &KCoreDirLister::completed, receiver, &Receiver::updateCount);
lister->setAutoUpdate(true);
lister->openUrl(QUrl(QStringLiteral("trash:/")));
```

`items()` returns the currently listed `KFileItemList`. Re-reading that list
from the lister's change signals keeps the count live without timer polling.
The initial `completed()` signal covers the asynchronous first listing.

## Opening Trash

`KIO::OpenUrlJob` opens the virtual Trash location. KIO jobs are started with a
`KNotificationJobUiDelegate` so failures use KDE's normal notification UI:

```cpp
#include <KIO/OpenUrlJob>
#include <KNotificationJobUiDelegate>

auto *job = new KIO::OpenUrlJob(QUrl(QStringLiteral("trash:/")));
job->setUiDelegate(new KNotificationJobUiDelegate(KJobUiDelegate::AutoErrorHandlingEnabled));
job->start();
```

## Moving URLs to Trash and emptying it

The KIO jobs create operations for moving URLs to Trash and emptying it:

```cpp
#include <KIO/CopyJob>
#include <KIO/EmptyTrashJob>

KIO::CopyJob *moveJob = KIO::trash(urls);
KIO::EmptyTrashJob *emptyJob = KIO::emptyTrash();
```

Both jobs use the same notification delegate. `emptyTrash()` is exposed to the
menu layer, which must ask for confirmation before invoking it.

## Theme icons and logical levels

`QIcon::hasThemeIcon()` checks whether the optional non-empty variant exists.
The empty state uses `user-trash`; a non-empty state uses `user-trash-full` when
provided by the active theme and otherwise falls back to `user-trash`.

Trash count is also exposed as four stable logical bands for tooltip and
accessibility consumers:

| Level | Count |
|-------|-------|
| `Empty` | 0 |
| `Few` | 1–4 |
| `Moderate` | 5–19 |
| `Many` | 20 or more |

The thresholds are presentation bands, not an estimate of disk usage.

## Build dependencies

The target links `KF6::KIOGui` and `KF6::JobWidgets`; KIO's core directory
lister and file-operation jobs are provided transitively by the KIO component.
