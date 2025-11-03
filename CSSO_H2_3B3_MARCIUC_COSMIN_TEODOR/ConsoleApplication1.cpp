#include <windows.h>
#include <stdio.h>
#include <string.h>
#include<map>
#include<string>
#include<vector>
#include<winver.h>
#pragma comment(lib, "version.lib")
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#define _CRT_SECURE_NO_WARNINGS
#define MAX_SIZE 1024
using namespace std;
map<string, int>extensionsMap;
vector<string> allowedExtensions = { ".exe", ".com", ".bat", ".cmd", ".ps1", ".vbs", ".js", ".msc" };
vector<string> files;
bool isAllowedExtension(char* ext) {
    if (!ext) return false;

    for (auto& e : allowedExtensions) {
        if (strcmp(ext, e.c_str()) == 0)
            return true;
    }
    return false;
}
void formatFileTime(const FILETIME* ft, char* formatedString, int size) {
    FILETIME localTime;
    FileTimeToLocalFileTime(ft, &localTime);
    SYSTEMTIME systemTime;
    FileTimeToSystemTime(&localTime, &systemTime);
    snprintf(formatedString, size, "%04d/%02d/%02d %02d:%02d:%02d", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
}
void processVersionInfo(char* path, HANDLE hout) {
    DWORD bytesWritten;
    DWORD useless;
    DWORD size = GetFileVersionInfoSizeA(path, &useless);
    char buffer[MAX_SIZE];
    if (size == 0) {
        sprintf(buffer, "File: %s no version info available.\r\n\r\n", path);
        WriteFile(hout, buffer, strlen(buffer), &bytesWritten, NULL);
        return;
    }
    BYTE* data = (BYTE*)malloc(size);
    if (!GetFileVersionInfoA(path, 0, size, data)) {
        free(data);
        sprintf(buffer, "file: %s  Failed to read version info.\r\n\r\n", path);
        WriteFile(hout, buffer, strlen(buffer), &bytesWritten, NULL);
        return;
    }
    struct LANGANDCODE {
      WORD wLanguage;
        WORD wCodePage;
    } *lpTranslate;
    UINT cbTranslate = 0;
    if (!VerQueryValueA(data, "\\VarFileInfo\\Translation", (LPVOID*)&lpTranslate, &cbTranslate)) {
        free(data);
        sprintf(buffer, "file: %s no translation info available.\r\n\r\n", path);
        WriteFile(hout, buffer, strlen(buffer), &bytesWritten, NULL);
        return;
    }

    const char* fields[] = { "CompanyName", "ProductName", "FileVersion" };
    char subBlock[MAX_SIZE];
    sprintf(buffer, "file: %s\r\n", path);
    WriteFile(hout, buffer, strlen(buffer), &bytesWritten, NULL);

    for (int i = 0; i < 3; i++) {
        char* value = NULL;
        UINT sizeVal = 0;
        sprintf(subBlock, "\\StringFileInfo\\%04x%04x\\%s",
            lpTranslate[0].wLanguage, lpTranslate[0].wCodePage, fields[i]);
        if (VerQueryValueA(data, subBlock, (LPVOID*)&value, &sizeVal)) {
            sprintf(buffer, "%s: %s\r\n", fields[i], value);
            WriteFile(hout, buffer, strlen(buffer), &bytesWritten, NULL);
        }
    }

    WriteFile(hout, "\r\n", 2, &bytesWritten, NULL);
    free(data);
}
void logCData(WIN32_FIND_DATA findData, HANDLE hout, char* extension) {
    char creationTime[MAX_SIZE];
    char lastAccessTime[MAX_SIZE];
    char lastWriteTime[MAX_SIZE];
    bool isExec = isAllowedExtension(extension);
    formatFileTime(&findData.ftCreationTime, creationTime, MAX_SIZE);
    formatFileTime(&findData.ftLastAccessTime, lastAccessTime, MAX_SIZE);
    formatFileTime(&findData.ftLastWriteTime, lastWriteTime, MAX_SIZE);
    char buffer[MAX_SIZE];
    snprintf(buffer, MAX_SIZE, "Name= %s CreationTime= %s LastAccessTime= %s LastWriteTime= %s IsExecutable= %d \r\n", findData.cFileName, creationTime, lastAccessTime, lastWriteTime, isExec);
    DWORD bytes;
    WriteFile(hout, buffer, strlen(buffer), &bytes, NULL);
   
}

void copyFiles(char* src, const char* dest) {
    char destPath[MAX_SIZE];
    sprintf(destPath, "%s\\%s", dest, strrchr(src, '\\') + 1);
    if (!CopyFileA(src, destPath, FALSE)) {
        printf("Eroare la copiere: %s (cod: %lu)\n", src, GetLastError());
    }
}
void createDirectories(const char* fullPath) {
    char auxPath[MAX_SIZE];
    char currentPath[MAX_SIZE] = "";
    strcpy(auxPath, fullPath);

    char* p = strtok(auxPath, "\\");
    while (p != NULL) {
        strcat(currentPath, p);

        // Creeaz directorul doar daca nu exista deja
        if (!CreateDirectoryA(currentPath, NULL)) {
            DWORD error = GetLastError();
            if (error != ERROR_ALREADY_EXISTS) {
                printf("Eroare la crearea directorului %s (eroare %lu)\n", currentPath, error);
                return;
            }
        }

        strcat(currentPath, "\\");
        p = strtok(NULL, "\\");
    }
}
void searchDirectoryRecursive(const char* searchDirectoryPath, HANDLE logFile ) {
    char searchPatternDirectory[MAX_SIZE];
    sprintf(searchPatternDirectory, "%s\\*", searchDirectoryPath);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPatternDirectory, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("Eroare la cautarea fisierului (eroare %lu)\n", GetLastError());
        return;
    }

    do {
        //printf("%s\n", findData.cFileName);
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0)//parent directory and currentdirecoty ignore
        {
            continue;
        }
        char currentPath[MAX_SIZE];
        sprintf(currentPath, "%s\\%s", searchDirectoryPath, findData.cFileName);
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            //printf("folder: %s\n", currentPath);
            searchDirectoryRecursive(currentPath, logFile);
        }
        else {
            //printf("file: %s\n",currentPath);
            char* extension = strrchr(currentPath, '.');
            logCData(findData, logFile, extension);
            if (isAllowedExtension(extension)) {
                files.push_back(string(currentPath));
            }
        }
    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}
void processRegisterKeys(HKEY roothive, const char* inputPath, HANDLE hout) {
    HKEY hkey;
    LSTATUS lstatus = RegOpenKeyExA(roothive, inputPath, 0, KEY_READ, &hkey);
    if (lstatus != ERROR_SUCCESS) {
        printf("Eroare la deschiderea cheii (%lu)\n", GetLastError());
        return;
    }

    DWORD lpcSubKeys = 0, lpcbMaxSubKeyLen = 0, lpcValues = 0, lpcbMaxValueNameLen = 0, lpcbMaxValueLen = 0;
    FILETIME lastWrite;
    RegQueryInfoKeyA(hkey, NULL, NULL, NULL, &lpcSubKeys, &lpcbMaxSubKeyLen, NULL, &lpcValues, &lpcbMaxValueNameLen, &lpcbMaxValueLen, NULL, &lastWrite);

    char formatedString[MAX_SIZE];
    formatFileTime(&lastWrite, formatedString, MAX_SIZE);
    char buffer[MAX_SIZE];
    DWORD bytesWritten;

    int n = snprintf(buffer, sizeof(buffer),
        "HiveRoot=%s Key=%s SubKeys=%lu Values=%lu LastWrite=%s\r\n",
        (roothive == HKEY_LOCAL_MACHINE) ? "HKLM" : "HKCU", inputPath, lpcSubKeys, lpcValues, formatedString);
    WriteFile(hout, buffer, n, &bytesWritten, NULL);

    for (DWORD i = 0; i < lpcValues; i++) {
        n = 0;  
        char valueName[MAX_SIZE];
        DWORD valueNameSize = sizeof(valueName);
        BYTE dataBuffer[MAX_SIZE * 8];
        DWORD dataSize = sizeof(dataBuffer);
        DWORD type = 0;

        LSTATUS e = RegEnumValueA(hkey, i, valueName, &valueNameSize, NULL, &type, dataBuffer, &dataSize);
        if (e == ERROR_NO_MORE_ITEMS || e != ERROR_SUCCESS) break;

        n += snprintf(buffer, sizeof(buffer), "  ValueName=%s Type=%u RawSize=%lu ", valueName, type, dataSize);

        if (type == REG_SZ || type == REG_EXPAND_SZ) {
            ((char*)dataBuffer)[dataSize >= 1 ? dataSize - 1 : 0] = '\0';
            char expandedResult[MAX_SIZE * 2] = { 0 };
            DWORD expandedSize = ExpandEnvironmentStringsA((char*)dataBuffer, expandedResult, sizeof(expandedResult));

            BOOL exists = FALSE;
            WIN32_FILE_ATTRIBUTE_DATA fileAttributes;
            if (expandedSize > 0 && expandedSize < sizeof(expandedResult)) {
                exists = GetFileAttributesExA(expandedResult, GetFileExInfoStandard, &fileAttributes);
            }

            n += snprintf(buffer + n, sizeof(buffer) - n,
                "Raw=%s Expanded=%s Exists=%d\r\n", (char*)dataBuffer, expandedResult, exists);
            WriteFile(hout, buffer, n, &bytesWritten, NULL);
            if (exists) {
                files.push_back(string(expandedResult));
            }
        }
        else {
            n += snprintf(buffer + n, sizeof(buffer) - n, "RawHex=");
            for (DWORD k = 0; k < dataSize && k < 64; k++) {
                n += snprintf(buffer + n, sizeof(buffer) - n, "%02X", dataBuffer[k]);
            }
            n += snprintf(buffer + n, sizeof(buffer) - n, "\r\n");
            WriteFile(hout, buffer, n, &bytesWritten, NULL);
        }
    }
    WriteFile(hout, "\r\n", 2, &bytesWritten,NULL);
    RegCloseKey(hkey);
}
void addToRegistryRun(const char* valueName, const char* exePath) {
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER,
        "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, valueName, 0, REG_SZ, (BYTE*)exePath, strlen(exePath) + 1);
        RegCloseKey(hKey);
    }
    else {
        printf("Eroare la deschiderea cheii Run din registry\n");
    }
}
void processSha256(char* filePath, HANDLE hout) {
    HANDLE hFile = CreateFileA(filePath, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return;
    }
    BCRYPT_ALG_HANDLE hAlg = NULL;
    BCRYPT_HASH_HANDLE hHash = NULL;
    BYTE hash[32]; // 256 bits = 32 bytes
    DWORD hashLen = sizeof(hash);
    char buf[256];
    DWORD written;
    if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, 0) != 0) {
        CloseHandle(hFile);
        return;
    }
    if (BCryptCreateHash(hAlg, &hHash, NULL, 0, NULL, 0, 0) != 0) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        CloseHandle(hFile);
        return;
    }
    BYTE buffer[4096];///standar memory page
    DWORD bytesRead;

    while (ReadFile(hFile, buffer, sizeof(buffer), &bytesRead, NULL) && bytesRead > 0) {
        BCryptHashData(hHash, buffer, bytesRead, 0);
    }

    BCryptFinishHash(hHash, hash, hashLen, 0);

    sprintf(buf, "SHA256(%s) = ", filePath);
    WriteFile(hout, buf, strlen(buf), &written, NULL);

    for (DWORD i = 0; i < hashLen; i++) {
        sprintf(buf, "%02X", hash[i]);
        WriteFile(hout, buf, 2, &written, NULL);
    }
    WriteFile(hout, "\r\n", 2, &written, NULL);

    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    CloseHandle(hFile);
}
void setStartupExecutablesCount(DWORD count) {
    HKEY hKey;
    DWORD disposition;
    if (RegCreateKeyExA(HKEY_CURRENT_USER,"Software\\CSSO\\Week2",0,NULL,0,KEY_WRITE,NULL,&hKey,&disposition) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "StartupExecutables", 0,REG_DWORD, (BYTE*)&count, sizeof(DWORD)); RegCloseKey(hKey);
    }
    else {
        printf("Eroare la crearea cheii HKCU\\Software\\CSSO\\Week2\n");
    }
}

int main() {
    const char* path = "C:\\Facultate\\CSSO\\Laboratoare\\H2\\RunningSoftware";
    createDirectories(path);

    char filePath[MAX_SIZE];
    sprintf(filePath, "%s\\fromRegistries.txt", path);
    HANDLE fh = CreateFileA(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        printf("Eroare la crearea fisierului fromRegistries.txt (%lu)\n", GetLastError());
        return 1;
    }
    processRegisterKeys(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", fh);
    processRegisterKeys(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", fh);
    processRegisterKeys(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run", fh);
    processRegisterKeys(HKEY_CURRENT_USER, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce", fh);
    CloseHandle(fh);


    char cFilePath[MAX_SIZE];
    sprintf(cFilePath,"%s\\fromStartupFolder.txt",path);
    HANDLE fhc = CreateFileA(cFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fhc == INVALID_HANDLE_VALUE) {
        printf("Eroare la crearea fisierului fromStarupFolder.txt (%lu)\n", GetLastError());
        return 1;
    }
    char userStartUp[MAX_SIZE];
    char allStartUp[MAX_SIZE];
    ExpandEnvironmentStringsA("%APPDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup", userStartUp, MAX_SIZE);
    ExpandEnvironmentStringsA("%PROGRAMDATA%\\Microsoft\\Windows\\Start Menu\\Programs\\Startup", allStartUp, MAX_SIZE); 
    searchDirectoryRecursive(userStartUp, fhc);
    searchDirectoryRecursive(allStartUp, fhc);
    CloseHandle(fhc);

    char dFilePath[MAX_SIZE];
    sprintf(dFilePath, "%s\\versionInfo.txt", path);
    HANDLE fhd = CreateFileA(dFilePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fhd == INVALID_HANDLE_VALUE) {
        printf("Eroare la crearea fisierului versionInfo.txt (%lu)\n", GetLastError());
        return 1;
    }
    for (auto f : files) {
        processVersionInfo((char*)f.c_str(), fhd);
    }
    CloseHandle(fhd);
    char hashFilePath[MAX_SIZE];
    sprintf(hashFilePath, "%s\\hashes.txt", path);
    HANDLE fhHash = CreateFileA(hashFilePath, GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fhHash == INVALID_HANDLE_VALUE) {
        printf("Eroare la crearea fisierului hashes.txt (%lu)\n", GetLastError());
        return 1;
    }
    for (auto f : files) {
        processSha256((char*)f.c_str(), fhHash);
    }
    CloseHandle(fhHash);

    const char* exeRegistry = "C:\\Windows\\System32\\calc.exe"; 
    const char* exeStartup = "C:\\Facultate\\CSSO\\Laboratoare\\H2\\RunningSoftware\\MyApp.exe"; 

    addToRegistryRun("MyCalcLauncher", exeRegistry);
    copyFiles((char*)exeStartup, userStartUp);

    setStartupExecutablesCount(DWORD(files.size()));

    return 0;
}
