/*
 * (c) Copyright Ascensio System SIA 2010-2019
 *
 * This program is a free software product. You can redistribute it and/or
 * modify it under the terms of the GNU Affero General Public License (AGPL)
 * version 3 as published by the Free Software Foundation. In accordance with
 * Section 7(a) of the GNU AGPL its Section 15 shall be amended to the effect
 * that Ascensio System SIA expressly excludes the warranty of non-infringement
 * of any third-party rights.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR  PURPOSE. For
 * details, see the GNU AGPL at: http://www.gnu.org/licenses/agpl-3.0.html
 *
 * You can contact Ascensio System SIA at 20A-12 Ernesta Birznieka-Upisha
 * street, Riga, Latvia, EU, LV-1050.
 *
 * The  interactive user interfaces in modified source and object code versions
 * of the Program must display Appropriate Legal Notices, as required under
 * Section 5 of the GNU AGPL version 3.
 *
 * Pursuant to Section 7(b) of the License you must retain the original Product
 * logo when distributing the program. Pursuant to Section 7(e) we decline to
 * grant you any rights under trademark law for use of our trademarks.
 *
 * All the Product's GUI elements, including illustrations and icon sets, as
 * well as technical writing content are licensed under the terms of the
 * Creative Commons Attribution-ShareAlike 4.0 International. See the License
 * terms at http://creativecommons.org/licenses/by-sa/4.0/legalcode
 *
 */

#include "cunzip.h"
#include "cunzip_p.h"
#include "utils.h"


int unzipArchive(const wstring &zipFilePath, const wstring &folderPath, std::atomic_bool &run)
{
    if (!NS_File::fileExists(zipFilePath) || !NS_File::dirExists(folderPath))
        return UNZIP_ERROR;

    wstring file = NS_File::toNativeSeparators(zipFilePath);
    wstring path = NS_File::toNativeSeparators(folderPath);

    HZIP hz = OpenZip(file.c_str(), 0);
    if (!hz)
        return UNZIP_ERROR;

    ZRESULT zr = SetUnzipBaseDir(hz, path.c_str());
    if (zr != ZR_OK) {
        CloseZip(hz);
        return UNZIP_ERROR;
    }

    ZIPENTRY ze = {0};
    zr = GetZipItem(hz, -1, &ze);
    if (zr != ZR_OK) {
        CloseZip(hz);
        return UNZIP_ERROR;
    }

    int count = ze.index;
    for (int i = 0; i < count; i++) {
        if (!run) {
            CloseZip(hz);
            return UNZIP_ABORT;
        }
        zr = GetZipItem(hz, i, &ze);
        if (zr != ZR_OK) {
            CloseZip(hz);
            return UNZIP_ERROR;
        }
        zr = UnzipItem(hz, i, ze.name);
        if (zr != ZR_OK && ze.unc_size != 0) {
            CloseZip(hz);
            return UNZIP_ERROR;
        }
    }

    CloseZip(hz);
    return UNZIP_OK;
}

CUnzip::CUnzip()
{
    m_run = false;
}

CUnzip::~CUnzip()
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
}

void CUnzip::extractArchive(const wstring &zipFilePath, const wstring &folderPath)
{
    m_run = false;
    if (m_future.valid())
        m_future.wait();
    m_run = true;
    m_future = std::async(std::launch::async, [=]() {
        int res = unzipArchive(zipFilePath, folderPath, m_run);
        if (m_complete_callback)
            m_complete_callback(res);
    });
}

void CUnzip::stop()
{
    m_run = false;
}

void CUnzip::onComplete(FnVoidInt callback)
{
    m_complete_callback = callback;
}
