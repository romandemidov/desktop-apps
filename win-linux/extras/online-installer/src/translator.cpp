#include "translator.h"
#include "resource.h"
#include "utils.h"
#include <Windows.h>
#include <cwctype>
//#include <iostream>

//using std::wcout;
//using std::endl;


bool isSeparator(wchar_t c)
{
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

bool isValidStringIdCharacter(wchar_t c)
{
    return std::iswalnum(c) || std::iswalpha(c) || c == L'_';
}

wstring StrToWStr(const char* str)
{
    wstring wstr;
    {
        size_t len = strlen(str), outSize = 0;
        wchar_t *pDestBuf = new wchar_t[len + 1];
        mbstowcs_s(&outSize, pDestBuf, len + 1, str, len);
        if (outSize > 0)
            wstr = pDestBuf;
        else
            NS_Logger::WriteLog(DEFAULT_ERROR_MESSAGE);
        delete[] pDestBuf;
    }
    return wstr;
}

TranslationsMap Translator::translMap = TranslationsMap();
wstring Translator::langName = _T("en");
bool Translator::is_translations_valid = false;

Translator::Translator(unsigned short langId, int resourceId)
{
    TCHAR _langName[LOCALE_NAME_MAX_LENGTH] = {0};
    int res = GetLocaleInfo(PRIMARYLANGID(langId), LOCALE_SISO639LANGNAME, _langName, LOCALE_NAME_MAX_LENGTH);
    if (res > 0)
        langName = _langName;
    else
        NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);

    NS_Logger::WriteLog(_T("Current locale: ") + langName);

    HMODULE hInst = GetModuleHandle(NULL);
    if (HRSRC hRes = FindResource(hInst, MAKEINTRESOURCE(resourceId), RT_RCDATA)) {
        if (HGLOBAL hResData = LoadResource(hInst, hRes)) {
            if (LPVOID pData = LockResource(hResData))
                translations = StrToWStr((const char*)pData);
            else
                NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
            FreeResource(hResData);
        } else
            NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);
    } else
        NS_Logger::WriteLog(ADVANCED_ERROR_MESSAGE);

    if (!translations.empty())
        parseTranslations();
    else
        NS_Logger::WriteLog(_T("Error: translations is empty."));
}

Translator::~Translator()
{

}

wstring Translator::tr(const char *str)
{
    wstring translatedStr = StrToWStr(str);
    if (is_translations_valid) {
        for (auto &strIdPair : translMap) {
            //LocaleMap locMap = strIdPair.second;
            for (LocaleMap::const_iterator it = strIdPair.second.begin(); it != strIdPair.second.end(); ++it) {
                //wcout << L"\n\n" << translatedStr << L"\n" << it->second;
                if (it->second == translatedStr) {
                    if (strIdPair.second.find(langName) != strIdPair.second.end())
                        translatedStr = strIdPair.second[langName];
                    break;
                }
            }
        }
    }
    return translatedStr;
}

void Translator::parseTranslations()
{
    int token = TOKEN_BEGIN_DOCUMENT;
    wstring stringId, currentLocale;
    size_t pos = 0, len = translations.length();
    while (pos < len) {
        size_t incr = 1;
        wchar_t ch = translations.at(pos);

        switch (token) {
        case TOKEN_BEGIN_DOCUMENT:
            //wcout << "BEGIN_DOCUMENT: " << ch << endl;
            if (!isSeparator(ch)) {
                if (isValidStringIdCharacter(ch)) {
                    token = TOKEN_BEGIN_STRING_ID;
                    continue;
                } else {
                    // TOKEN_ERROR
                    error_substr = translations.substr(0, pos);
                    return;
                }
            }
            break;

        case TOKEN_BEGIN_STRING_ID: {
            //wcout << "BEGIN_STRING_ID: " << ch << endl;
            size_t end;
            for (end = pos; end < len; end++) {
                wchar_t c = translations.at(end);
                if (!isValidStringIdCharacter(c))
                    break;
            }
            if ((end + 1) < len && !isSeparator(translations.at(end + 1))) {
                // TOKEN_ERROR
                error_substr = translations.substr(0, pos + 1);
                return;
            }
            stringId = translations.substr(pos, end - pos);
            //wcout << "BEGIN_STRING_ID: " << stringId << endl;
            translMap[stringId] = LocaleMap();

            token = TOKEN_END_STRING_ID;
            incr = end - pos + 1;
            break;
        }

        case TOKEN_END_STRING_ID:
        case TOKEN_END_VALUE: {
            if (!isSeparator(ch)) {
                size_t end;
                for (end = pos; end < len; end++) {
                    wchar_t c = translations.at(end);
                    if (!std::iswalpha(c))
                        break;
                }
                if (end - pos == 2) {
                    token = TOKEN_BEGIN_LOCALE;
                    continue;
                } else {
                    if (isValidStringIdCharacter(ch)) {
                        token = TOKEN_BEGIN_STRING_ID;
                        continue;
                    } else {
                        // TOKEN_ERROR
                        error_substr = translations.substr(0, pos);
                        return;
                    }
                }
            }
            break;
        }

        case TOKEN_BEGIN_LOCALE: {
            currentLocale = translations.substr(pos, 2);
            //wcout << "BEGIN_LOCALE: " << loc << endl;
            token = TOKEN_END_LOCALE;
            incr = 3;
            break;
        }

        case TOKEN_END_LOCALE:
            //wcout << "END_LOCALE: " << ch << endl;
            if (!isSeparator(ch)) {
                if (ch == L'=') {
                    token = TOKEN_BEGIN_VALUE;
                } else {
                    // TOKEN_ERROR
                    error_substr = translations.substr(0, pos);
                    return;
                }
            }
            break;

        case TOKEN_BEGIN_VALUE: {
            size_t end = translations.find_first_of(L'\n', pos);
            wstring val;
            if (end == wstring::npos) {
                val = translations.substr(pos);
                incr = len - pos;
            } else {
                val = translations.substr(pos, end - pos);
                incr = end - pos + 1;
            }

            if (!val.empty() && val.back() == L'\r')
                val.pop_back();

            size_t p = val.find(L"\\n");
            while (p != std::string::npos) {
                val.replace(p, 2, L"\\");
                val[p] = L'\n';
                p = val.find(L"\\n", p + 1);
            }

            if (!currentLocale.empty() && translMap.find(stringId) != translMap.end())
                translMap[stringId][currentLocale] = val;
            //wcout << "BEGIN_VALUE: " << val << endl;
            token = TOKEN_END_VALUE;
            break;
        }

        default:
            break;
        }
        pos += incr;
        if (pos == len)
            token = TOKEN_END_DOCUMENT;
    }

    if (token == TOKEN_END_DOCUMENT)
        is_translations_valid = true;
    else
        NS_Logger::WriteLog(_T("Cannot parse translations, error in string: ") + error_substr + L" <---");
}


