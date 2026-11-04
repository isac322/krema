// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

function textUrls(drop) {
    let data = ""
    if (typeof drop.getDataAsString === "function") {
        data = drop.getDataAsString("text/uri-list")
    }
    if (!data && typeof drop.text === "string") {
        data = drop.text
    }
    if (!data) {
        return []
    }

    let urls = []
    let lines = data.split(/\r\n|\n|\r/)
    for (let i = 0; i < lines.length; i++) {
        let uri = lines[i].trim()
        if (!uri || uri[0] === "#"
                || !/^[A-Za-z][A-Za-z0-9+.-]*:/.test(uri)) {
            continue
        }
        urls.push(Qt.resolvedUrl(uri))
    }
    return urls
}

function isLocalFile(url) {
    return String(url).toLowerCase().indexOf("file:") === 0
}

function normalizedUrls(drop) {
    let fallback = textUrls(drop)
    if (!drop.hasUrls || !drop.urls || drop.urls.length === 0) {
        return fallback
    }

    let urls = []
    for (let i = 0; i < drop.urls.length; i++) {
        urls.push(drop.urls[i])
    }

    // Qt's Wayland portal can replace a local file URL with a synthetic HTTP
    // transfer URL when portal access fails. The original text/uri-list still
    // carries the local file and is the authoritative payload in that case.
    let hasLocalFallback = fallback.some(isLocalFile)
    let hasLocalUrl = urls.some(isLocalFile)
    if (hasLocalFallback && !hasLocalUrl) {
        return fallback
    }
    return urls
}
