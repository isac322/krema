// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

function parseUris(data) {
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
    let mimeUrls = typeof drop.getDataAsString === "function"
        ? parseUris(drop.getDataAsString("text/uri-list")) : []
    let textUrls = typeof drop.text === "string" ? parseUris(drop.text) : []
    let fallback = mimeUrls.length > 0 ? mimeUrls : textUrls
    if (!mimeUrls.some(isLocalFile) && textUrls.some(isLocalFile)) {
        fallback = textUrls
    }
    if (!drop.hasUrls || !drop.urls || drop.urls.length === 0) {
        return fallback
    }

    let urls = []
    for (let i = 0; i < drop.urls.length; i++) {
        urls.push(drop.urls[i])
    }

    // Qt's Wayland portal can replace a local file URL with a synthetic HTTP
    // transfer URL when portal access fails. Its independent text payload can
    // still carry the original local file URI in that case.
    let hasLocalFallback = fallback.some(isLocalFile)
    let hasLocalUrl = urls.some(isLocalFile)
    if (hasLocalFallback && !hasLocalUrl) {
        return fallback
    }
    return urls
}
