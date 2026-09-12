// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors

#include "peiconextractor.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QtEndian>

#pragma pack(push, 1)
struct IMAGE_DOS_HEADER {
    quint16 e_magic;
    quint16 e_cblp[29];
    quint32 e_lfanew;
};
struct IMAGE_FILE_HEADER {
    quint16 Machine;
    quint16 NumberOfSections;
    quint32 TimeDateStamp;
    quint32 PointerToSymbolTable;
    quint32 NumberOfSymbols;
    quint16 SizeOfOptionalHeader;
    quint16 Characteristics;
};
struct IMAGE_DATA_DIRECTORY {
    quint32 VirtualAddress;
    quint32 Size;
};
struct IMAGE_SECTION_HEADER {
    char Name[8];
    quint32 VirtualSize;
    quint32 VirtualAddress;
    quint32 SizeOfRawData;
    quint32 PointerToRawData;
    quint32 PointerToRelocations;
    quint32 PointerToLinenumbers;
    quint16 NumberOfRelocations;
    quint16 NumberOfLinenumbers;
    quint32 Characteristics;
};
struct IMAGE_RESOURCE_DIRECTORY {
    quint32 Characteristics;
    quint32 TimeDateStamp;
    quint16 MajorVersion;
    quint16 MinorVersion;
    quint16 NumberOfNamedEntries;
    quint16 NumberOfIdEntries;
};
struct IMAGE_RESOURCE_DIRECTORY_ENTRY {
    quint32 Name;
    quint32 OffsetToData;
};
struct IMAGE_RESOURCE_DATA_ENTRY {
    quint32 OffsetToData;
    quint32 Size;
    quint32 CodePage;
    quint32 Reserved;
};
struct GRPICONDIR {
    quint16 idReserved;
    quint16 idType;
    quint16 idCount;
};
struct GRPICONDIRENTRY {
    quint8 bWidth;
    quint8 bHeight;
    quint8 bColorCount;
    quint8 bReserved;
    quint16 wPlanes;
    quint16 wBitCount;
    quint32 dwBytesInRes;
    quint16 nID;
};
#pragma pack(pop)

static quint32 rvaToOffset(quint32 rva, const QVector<IMAGE_SECTION_HEADER> &sections)
{
    for (const auto &sec : sections) {
        if (rva >= sec.VirtualAddress && rva < sec.VirtualAddress + std::max(sec.VirtualSize, sec.SizeOfRawData)) {
            return rva - sec.VirtualAddress + sec.PointerToRawData;
        }
    }
    return 0;
}

static quint32 getResourceDataOffset(QFile &file, quint32 rsrcOffset, quint32 typeId, quint32 nameId)
{
    auto readDir = [&](quint32 offset) -> IMAGE_RESOURCE_DIRECTORY {
        IMAGE_RESOURCE_DIRECTORY dir;
        file.seek(rsrcOffset + offset);
        file.read(reinterpret_cast<char *>(&dir), sizeof(dir));
        return dir;
    };

    IMAGE_RESOURCE_DIRECTORY typeDir = readDir(0);
    quint32 level2Offset = 0;
    for (int i = 0; i < typeDir.NumberOfNamedEntries + typeDir.NumberOfIdEntries; ++i) {
        IMAGE_RESOURCE_DIRECTORY_ENTRY entry;
        file.read(reinterpret_cast<char *>(&entry), sizeof(entry));
        if (entry.Name == typeId && (entry.OffsetToData & 0x80000000)) {
            level2Offset = entry.OffsetToData & 0x7FFFFFFF;
            break;
        }
    }
    if (!level2Offset)
        return 0;

    IMAGE_RESOURCE_DIRECTORY nameDir = readDir(level2Offset);
    quint32 level3Offset = 0;
    for (int i = 0; i < nameDir.NumberOfNamedEntries + nameDir.NumberOfIdEntries; ++i) {
        IMAGE_RESOURCE_DIRECTORY_ENTRY entry;
        file.read(reinterpret_cast<char *>(&entry), sizeof(entry));
        if ((nameId == 0 || entry.Name == nameId) && (entry.OffsetToData & 0x80000000)) {
            level3Offset = entry.OffsetToData & 0x7FFFFFFF;
            break;
        }
    }
    if (!level3Offset)
        return 0;

    IMAGE_RESOURCE_DIRECTORY langDir = readDir(level3Offset);
    if (langDir.NumberOfNamedEntries + langDir.NumberOfIdEntries == 0)
        return 0;
    IMAGE_RESOURCE_DIRECTORY_ENTRY langEntry;
    file.read(reinterpret_cast<char *>(&langEntry), sizeof(langEntry));

    IMAGE_RESOURCE_DATA_ENTRY dataEntry;
    file.seek(rsrcOffset + (langEntry.OffsetToData & 0x7FFFFFFF));
    file.read(reinterpret_cast<char *>(&dataEntry), sizeof(dataEntry));
    return dataEntry.OffsetToData;
}

QImage PeIconExtractor::extract(const QString &exePath)
{
    QFile file(exePath);
    if (!file.open(QIODevice::ReadOnly))
        return QImage();

    IMAGE_DOS_HEADER dosHeader;
    if (file.read(reinterpret_cast<char *>(&dosHeader), sizeof(dosHeader)) != sizeof(dosHeader) || dosHeader.e_magic != 0x5A4D)
        return QImage();

    file.seek(dosHeader.e_lfanew);
    quint32 peSignature = 0;
    file.read(reinterpret_cast<char *>(&peSignature), sizeof(peSignature));
    if (peSignature != 0x00004550)
        return QImage();

    IMAGE_FILE_HEADER fileHeader;
    file.read(reinterpret_cast<char *>(&fileHeader), sizeof(fileHeader));

    quint16 magic = 0;
    qint64 optHeaderPos = file.pos();
    file.read(reinterpret_cast<char *>(&magic), sizeof(magic));

    // DATA DIRECTORY OFFSETS (The Bug Fix):
    // PE32 (32-bit): Resource Directory is at offset 112 in Optional Header
    // PE32+ (64-bit): Resource Directory is at offset 128 in Optional Header
    quint32 rsrcRva = 0;
    if (magic == 0x10B)
        file.seek(optHeaderPos + 112);
    else if (magic == 0x20B)
        file.seek(optHeaderPos + 128);
    else
        return QImage();

    IMAGE_DATA_DIRECTORY rsrcDirHeader;
    file.read(reinterpret_cast<char *>(&rsrcDirHeader), sizeof(rsrcDirHeader));
    rsrcRva = rsrcDirHeader.VirtualAddress;
    if (rsrcRva == 0)
        return QImage();

    file.seek(dosHeader.e_lfanew + 4 + sizeof(IMAGE_FILE_HEADER) + fileHeader.SizeOfOptionalHeader);
    QVector<IMAGE_SECTION_HEADER> sections(fileHeader.NumberOfSections);
    file.read(reinterpret_cast<char *>(sections.data()), fileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER));

    quint32 rsrcOffset = rvaToOffset(rsrcRva, sections);
    if (rsrcOffset == 0)
        return QImage();

    quint32 groupDataRva = getResourceDataOffset(file, rsrcOffset, 14, 0);
    if (groupDataRva == 0)
        return QImage();

    quint32 groupDataOffset = rvaToOffset(groupDataRva, sections);
    file.seek(groupDataOffset);
    GRPICONDIR groupHeader;
    file.read(reinterpret_cast<char *>(&groupHeader), sizeof(groupHeader));

    quint16 bestIconId = 0;
    int bestSize = 0;
    for (int i = 0; i < groupHeader.idCount; ++i) {
        GRPICONDIRENTRY entry;
        file.read(reinterpret_cast<char *>(&entry), sizeof(entry));
        int w = (entry.bWidth == 0 ? 256 : entry.bWidth);
        int h = (entry.bHeight == 0 ? 256 : entry.bHeight);
        if (w * h > bestSize) {
            bestSize = w * h;
            bestIconId = entry.nID;
        }
    }
    if (bestIconId == 0)
        return QImage();

    quint32 iconDataRva = getResourceDataOffset(file, rsrcOffset, 3, bestIconId);
    if (iconDataRva == 0)
        return QImage();

    quint32 iconDataOffset = rvaToOffset(iconDataRva, sections);
    file.seek(iconDataOffset);
    QByteArray rawIconData = file.read(1024 * 1024); // Grab up to 1MB

    if (rawIconData.startsWith("\x89PNG\r\n\x1A\n"))
        return QImage::fromData(rawIconData, "PNG");

    QByteArray fakeIcoFile;
    QDataStream out(&fakeIcoFile, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(1) << quint8(0) << quint8(0) << quint8(0) << quint8(0) << quint16(1) << quint16(32)
        << quint32(rawIconData.size()) << quint32(22);
    fakeIcoFile.append(rawIconData);

    return QImage::fromData(fakeIcoFile, "ICO");
}
