#include <windows.h>
#include <stdio.h>
#include <string.h>
#include<map>
#include<string>
#include<vector>
#define _CRT_SECURE_NO_WARNINGS
#define MAX_SIZE 1024
using namespace std;
map<string, int>extensionsMap;
vector<string>allowedExtensions;
bool isAllowedExtension(char* ext) {
    if (!ext) return false;

    for (auto& e : allowedExtensions) {
        if (strcmp(ext, e.c_str()) == 0)  
            return true;
    }
    return false;
}
void copyFilesAndLog(char* src, const char* dest, HANDLE logFile) {
    char destPath[MAX_SIZE];
    sprintf(destPath, "%s\\%s", dest, strrchr(src, '\\') + 1);//iau pathu unde va fi si adaug doar ultima parte gen main.cpp din src
    BOOL ok=CopyFileA(src, destPath, FALSE);
    if (ok) {
        char msg[MAX_SIZE * 2];
        sprintf(msg, "Copiat: %s -> %s\r\n", src, destPath);
        DWORD bytes;
        WriteFile(logFile, msg, strlen(msg), &bytes, NULL);
    }
    else {
        //log error
        char msg[MAX_SIZE * 2];
        sprintf(msg, "Eroare la copiere: %s (cod: %lu)\r\n", src, GetLastError());
        DWORD bytes;
        WriteFile(logFile, msg, strlen(msg),&bytes,NULL);
    }
}
void createDirectories(const char* fullPath) {
    char auxPath[MAX_SIZE];
    char currentPath[MAX_SIZE] = "";
    strcpy(auxPath, fullPath);

    char* p = strtok(auxPath, "\\");
    while (p != NULL) {
        strcat(currentPath, p);

        // Creeaz directorul doar dac? nu exist? deja
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
void searchDirectoryRecursive(const char* searchDirectoryPath,int mode=0,const char* destDir=NULL,HANDLE logFile=NULL) {
    char searchPatternDirectory[MAX_SIZE];
    sprintf(searchPatternDirectory, "%s\\*", searchDirectoryPath);
    WIN32_FIND_DATAA findData;
    HANDLE hFind = FindFirstFileA(searchPatternDirectory, &findData);
    if (hFind == INVALID_HANDLE_VALUE) {
        ///error handler later
        return;
    }    

    do {     
        //printf("%s\n", findData.cFileName);
        if (strcmp(findData.cFileName, ".")==0 || strcmp(findData.cFileName ,"..")==0)//parent directory and currentdirecoty ignore
        {
            continue;
        }
        char currentPath[MAX_SIZE];
        sprintf(currentPath, "%s\\%s", searchDirectoryPath, findData.cFileName);
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            //printf("folder: %s\n", currentPath);
            searchDirectoryRecursive(currentPath,mode,destDir,logFile);
        }
        else {
            //printf("file: %s\n",currentPath);
            char* extension = strrchr(currentPath, '.');
            if (extension) {
                if(mode==0)
                extensionsMap[extension]++;
                else {
                    if (isAllowedExtension(extension)) {
                        copyFilesAndLog(currentPath, destDir, logFile);
                    }
                }
            }
        }

    } while (FindNextFileA(hFind, &findData));
    FindClose(hFind);
}
int main() {
    const char* path = "C:\\Facultate\\CSSO\\Labs\\H1\\Rezultate";
    createDirectories(path);
    printf("director succes!\n");

    char searchDirectoryPath[MAX_SIZE];
    fgets(searchDirectoryPath, MAX_SIZE, stdin);
    searchDirectoryPath[strlen(searchDirectoryPath) - 1] = '\0';
    searchDirectoryRecursive(searchDirectoryPath);
   
    char filePath[MAX_SIZE];
    sprintf(filePath, "%s\\sumar.txt", path);
    HANDLE fh = CreateFileA(filePath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,NULL);
    if (fh == INVALID_HANDLE_VALUE) {
        printf("Eroare la crearea fisierului sumar.txt (%lu)\n", GetLastError());
        return 1;
        //error handle;
    }

    char buffer[MAX_SIZE];
    DWORD bytes;
    WriteFile(fh, searchDirectoryPath, strlen(searchDirectoryPath), &bytes, NULL);
    for (auto &it : extensionsMap) {
        sprintf(buffer,",%s : %d ", it.first.c_str(), it.second);
        WriteFile(fh, buffer, strlen(buffer), &bytes, NULL);
    }
    CloseHandle(fh);
    printf("finished wiritng to sumar.txt\n");

    HANDLE hFile = CreateFileA("C:\\Facultate\\CSSO\\Labs\\H1\\Input\\copy.ini",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        printf("Nu pot deschide copy.ini (cod: %lu)\n", GetLastError());
        //error handle
        return -1;
    }
    char targetCopyDir[MAX_SIZE];
    bool firstLine = true;
    char line[MAX_SIZE];
    char c;
    int pos = 0;
    while (ReadFile(hFile,&c,1,&bytes,NULL) && bytes>0) {
        if (c == '\r') continue;
        if (c == '\n') {
            if (pos > 0) {
                line[pos] = '\0';
                if (firstLine) {
                    strcpy(targetCopyDir, line);
                    firstLine = false;
                }
                else {
                    allowedExtensions.push_back(line);
                }
            }
            pos = 0;
            continue;
        }
        if (pos < MAX_SIZE)
        {
            line[pos] = c;
            pos++;
        }
        
    }
    // Daca fisierul nu se termina cu '\n', proceseaza ultima linie
    if (pos > 0) {
        line[pos] = '\0';
        if (firstLine)
            strcpy(targetCopyDir, line);
        else
            allowedExtensions.push_back(line);
    }
    CloseHandle(hFile);
    //printf("director : %s\n", targetCopyDir);
    /*printf("extensii permise:\n");
    for (auto& ext : allowedExtensions)
        printf("  %s\n", ext.c_str());*/
    HANDLE logFile = CreateFileA("C:\\Facultate\\CSSO\\Labs\\H1\\Rezultate\\fisiereCopiate.txt",
        GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (logFile == INVALID_HANDLE_VALUE) {
        printf("Eroare la crearea logului!\n");
        return -1;
    }
    searchDirectoryRecursive(searchDirectoryPath, 1, targetCopyDir, logFile);

    return 0;
}
