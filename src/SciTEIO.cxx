// SciTE - Scintilla based Text Editor
// SciTEIO.cxx - manage input and output with the system
// Copyright 1998-2000 by Neil Hodgson <neilh@scintilla.org>
// The License.txt file describes the conditions under which this software may be distributed.

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>	// For time_t

#include "Platform.h"

#if PLAT_GTK

#include <unistd.h>

#endif

#if PLAT_WIN
// For getcwd and chdir
#ifdef _MSC_VER
#include <direct.h>
#endif
#ifdef __BORLANDC__
#include <dir.h>
#endif

#endif

#include "SciTE.h"
#include "PropSet.h"
#include "Accessor.h"
#include "WindowAccessor.h"
//#include "KeyWords.h"
//#include "ScintillaWidget.h"
#include "Scintilla.h"
//#include "SciLexer.h"
#include "Extender.h"
#include "SciTEBase.h"

#define PROPERTIES_EXTENSION	".properties"

static bool IsPropertiesFile(char *filename) {
	int nameLen = strlen(filename);
	int propLen = strlen(PROPERTIES_EXTENSION);
	if (nameLen <= propLen)
		return false;
	if (stricmp(filename + nameLen - propLen, PROPERTIES_EXTENSION) == 0)
		return true;
	return false;
}

bool SciTEBase::IsAbsolutePath(const char *path) {
	if (!path || *path == '\0')
		return false;
#ifdef unix
	if (path[0] == '/')
		return true;
#endif
#ifdef WIN32
	if (path[0] == '\\' || path[1] == ':')	// UNC path or drive separator
		return true;
#endif
	return false;
}

void SciTEBase::FixFilePath() {
	// Only used on Windows to fix the case of file names
}

#ifdef __vms
const char *VMSToUnixStyle(const char *fileName) {
	// possible formats:
	// o disk:[dir.dir]file.type
	// o logical:file.type
	// o [dir.dir]file.type
	// o file.type
	// o /disk//dir/dir/file.type
	// o /disk/dir/dir/file.type

	static char unixStyleFileName[MAX_PATH + 20];

	if (strchr(fileName, ':') == NULL && strchr(fileName, '[') == NULL) {
		// o file.type
		// o /disk//dir/dir/file.type
		// o /disk/dir/dir/file.type
		if (strstr (fileName, "//") == NULL) {
			return fileName;
		}
		strcpy(unixStyleFileName, fileName);
		char *p;
		while ((p = strstr (unixStyleFileName, "//")) != NULL) {
			strcpy (p, p + 1);
		}
		return unixStyleFileName;
	}

	// o disk:[dir.dir]file.type
	// o logical:file.type
	// o [dir.dir]file.type

	if (fileName [0] == '/') {
		strcpy(unixStyleFileName, fileName);
	} else {
		unixStyleFileName [0] = '/';
		strcpy(unixStyleFileName + 1, fileName);
		char *p = strstr(unixStyleFileName, ":[");
		if (p == NULL) {
			// o logical:file.type
			p = strchr(unixStyleFileName, ':');
			*p = '/';
		} else {
			*p = '/';
			strcpy(p + 1, p + 2);
			char *end = strchr(unixStyleFileName, ']');
			if (*end != NULL) {
				*end = '/';
			}
			while (p = strchr(unixStyleFileName, '.'), p != NULL && p < end) {
				*p = '/';
			}
		}
	}
	return unixStyleFileName;
} // VMSToUnixStyle
#endif

void SciTEBase::SetFileName(const char *openName, bool fixCase) {
	if (openName[0] == '\"') {
		char pathCopy[MAX_PATH + 1];
		pathCopy[0] = '\0';
		strncpy(pathCopy, openName + 1, MAX_PATH);
		pathCopy[MAX_PATH] = '\0';
		if (pathCopy[strlen(pathCopy) - 1] == '\"')
			pathCopy[strlen(pathCopy) - 1] = '\0';
		AbsolutePath(fullPath, pathCopy, MAX_PATH);
	} else if (openName[0]) {
		AbsolutePath(fullPath, openName, MAX_PATH);
	} else {
		fullPath[0] = '\0';
	}

	// Break fullPath into directory and file name using working directory for relative paths
	char *cpDirEnd = strrchr(fullPath, pathSepChar);
	if (IsAbsolutePath(fullPath)) {
		// Absolute path
			strcpy(fileName, cpDirEnd + 1);
			strcpy(dirName, fullPath);
			dirName[cpDirEnd - fullPath] = '\0';
		//Platform::DebugPrintf("SetFileName: <%s> <%s>\n", fileName, dirName);
	} else {
		// Relative path
		getcwd(dirName, sizeof(dirName));
		//Platform::DebugPrintf("Working directory: <%s>\n", dirName);
		if (cpDirEnd) {
			// directories and file name
			strcpy(fileName, cpDirEnd + 1);
			*cpDirEnd = '\0';
			strncat(dirName, pathSepString, sizeof(dirName));
			strncat(dirName, fullPath, sizeof(dirName));
		} else {
			// Just a file name
			strcpy(fileName, fullPath);
		}
	}
	
	// Rebuild fullPath from directory and name
	strcpy(fullPath, dirName);
	strcat(fullPath, pathSepString);
	strcat(fullPath, fileName);
	//Platform::DebugPrintf("Path: <%s>\n", fullPath);
	
	if (fixCase)
		FixFilePath();
	char fileBase[MAX_PATH];
	strcpy(fileBase, fileName);
	char *extension = strrchr(fileBase, '.');
	if (extension) {
		*extension = '\0';
		strcpy(fileExt, extension + 1);
	} else { // No extension
		fileExt[0] = '\0';
	}

	chdir(dirName);

	ReadLocalPropFile();

	props.Set("FilePath", fullPath);
	props.Set("FileDir", dirName);
	props.Set("FileName", fileBase);
	props.Set("FileExt", fileExt);
	props.Set("FileNameExt", fileName);

	SetWindowName();
	if (buffers.buffers)
		buffers.buffers[buffers.current].fileName = fullPath;
	BuffersMenu();
}

bool SciTEBase::Exists(const char *dir, const char *path, char *testPath) {
	char copyPath[MAX_PATH];
	if (IsAbsolutePath(path) || !dir) {
		strcpy(copyPath, path);
	}
	else if (dir) {
		if ((strlen(dir) + strlen(pathSepString) + strlen(path) + 1) > MAX_PATH)
			return false;
		strcpy(copyPath, dir);
		strcat(copyPath, pathSepString);
		strcat(copyPath, path);
	}
	FILE *fp = fopen(copyPath, fileRead);
	if (!fp)
		return false;
	fclose(fp);
	if (testPath)
	AbsolutePath(testPath, copyPath, MAX_PATH);
	return true;
}

time_t GetModTime(const char *fullPath) {
	struct stat statusFile;
	if (stat(fullPath, &statusFile) != -1)
		return statusFile.st_mtime;
	else
		return 0;
}

void SciTEBase::CountLineEnds(int &linesCR, int &linesLF, int &linesCRLF) {
	linesCR = 0;
	linesLF = 0;
	linesCRLF = 0;
	int lengthDoc = LengthDocument();
	char chPrev = ' ';
	WindowAccessor acc(wEditor.GetID(), props);
	for (int i = 0; i < lengthDoc; i++) {
		char ch = acc[i];
		char chNext = acc.SafeGetCharAt(i+1);
		if (ch == '\r') {
			if (chNext == '\n')
				linesCRLF++;
			else 
				linesCR++;
		} else if (ch == '\n') {
			if (chPrev != '\r') {
				linesLF++;
			}
		}
		chPrev = ch;
	}
}

void SciTEBase::OpenFile(bool initialCmdLine) {
	FILE *fp = fopen(fullPath, fileRead);
	if (fp || initialCmdLine) {
		// If initial run and no fp, just open an empty buffer
		// with the given name
		if (fp) {
			fileModTime = GetModTime(fullPath);

			SendEditor(SCI_CLEARALL);
			char data[blockSize];
			int lenFile = fread(data, 1, sizeof(data), fp);
			while (lenFile > 0) {
				SendEditorString(SCI_ADDTEXT, lenFile, data);
				lenFile = fread(data, 1, sizeof(data), fp);
		}
			fclose(fp);
			if (props.GetInt("eol.auto")) {
				int linesCR;
				int linesLF;
				int linesCRLF;
				CountLineEnds(linesCR, linesLF, linesCRLF);
				if ((linesLF > linesCR) && (linesLF > linesCRLF))
					SendEditor(SCI_SETEOLMODE, SC_EOL_LF);
				if ((linesCR > linesLF) && (linesCR > linesCRLF))
					SendEditor(SCI_SETEOLMODE, SC_EOL_CR);
				if ((linesCRLF > linesLF) && (linesCRLF > linesCR))
					SendEditor(SCI_SETEOLMODE, SC_EOL_CRLF);
		}
	}
		} else {
		char msg[MAX_PATH + 100];
		strcpy(msg, "Could not open file \"");
		strcat(msg, fullPath);
		strcat(msg, "\".");
		MessageBox(wSciTE.GetID(), msg, appName, MB_OK);
			}
	SendEditor(SCI_SETUNDOCOLLECTION, 1);
	// Flick focus to the output window and back to
	// ensure palette realised correctly.
	SetFocus(wOutput.GetID());
	SetFocus(wEditor.GetID());
	SendEditor(SCI_SETSAVEPOINT);
	if (props.GetInt("fold.on.open") > 0) {
		FoldAll();
		}
	SendEditor(SCI_GOTOPOS, 0);
	Redraw();
	}

void SciTEBase::Open(const char *file, bool initialCmdLine) {
	InitialiseBuffers();

	if (!file) {
		MessageBox(wSciTE.GetID(), "Bad file", appName, MB_OK);
	}
#ifdef __vms
	static char fixedFileName [MAX_PATH];
	strcpy(fixedFileName, VMSToUnixStyle(file));
	file = fixedFileName;
#endif
	
	int index = buffers.GetDocumentByName(file);
	if (index >= 0) {
		SetDocumentAt(index);
		DeleteFileStackMenu();
		SetFileStackMenu();
		return ;
	}

	if (buffers.size == buffers.length) {
		AddFileToStack(fullPath, GetSelection(), GetCurrentScrollPosition());
		ClearDocument();
	} else {
		New();
	}

	//Platform::DebugPrintf("Opening %s\n", file);
	SetFileName(file);
	overrideExtension = "";
	ReadProperties();
	UpdateBuffersCurrent();

	if (initialCmdLine) {
		if (props.GetInt("save.recent", 0))
			LoadRecentMenu();
	}
	if (fileName[0]) {
		SendEditor(SCI_CANCEL);
		SendEditor(SCI_SETUNDOCOLLECTION, 0);

		OpenFile(initialCmdLine);

		SendEditor(SCI_EMPTYUNDOBUFFER);
		}
	RemoveFileFromStack(fullPath);
	DeleteFileStackMenu();
	SetFileStackMenu();
	SetWindowName();
	if (extender)
		extender->OnOpen();
}

void SciTEBase::OpenSelected() {
	char selectedFilename[MAX_PATH];
	unsigned long lineNumber = 0;

	SelectionFilename(selectedFilename, sizeof(selectedFilename));
	if (selectedFilename[0] == '\0') {
		WarnUser(warnWrongFile);
		return;	// No selection
	}
	if (stricmp(selectedFilename, fileName) == 0) {
		WarnUser(warnWrongFile);
		return;	// Do not open if it is the current file!
	}
	if (IsPropertiesFile(fileName) &&
		strlen(fileName) + strlen(PROPERTIES_EXTENSION) < MAX_PATH) {
		// We are in a properties file, we append the correct extension
		// to open the include
		strcat(selectedFilename, PROPERTIES_EXTENSION);
	} else {
		// Check if we have a line number (error message or grep result)
		// A bit of duplicate work with DecodeMessage, but we don't know
		// here the format of the line, so we do guess work.
		char *endPath = strchr(selectedFilename, '(');
		if (endPath) {	// Visual Studio error: F:\scite\src\SciTEBase.h(312):	bool Exists(
			lineNumber = atol(endPath + 1);
			*(endPath - 1) = '\0';
		} else {
			char *endPath = strchr(selectedFilename + 2, ':');	// Skip Windows' drive separator
			if (endPath) {	// grep -n line, perhaps gcc too: F:\scite\src\SciTEBase.h:312:	bool Exists(
				lineNumber = atol(endPath + 1);
				*(endPath - 1) = '\0';
			}
		}
		// Can't do much for space separated line numbers...

//		if (strncmp(selectedFilename, "http", 4) == 0) {
//			SString cmd = selectedFilename;
//			ShellExec(cmd, NULL);
//		}
		// Currently don't work (have to declare virtual function in SciTEBase.h,
		// or to do another way), no much time to work on it.
		// It would be nice to make it work though.

		// Try to implement the ctags format...
		if (lineNumber == 0) {
			// To be done...
		}
	}

	char path[MAX_PATH];
	*path = '\0';
	// Don't load the path of the current file if the selected
	// filename is an absolute pathname
	if (!IsAbsolutePath(selectedFilename)) {
		getcwd(path, sizeof(path));
	}
	if (Exists(path, selectedFilename, path)) {
		if (CanMakeRoom()) {
			Open(path, false);	// TODO: test result?
			if (lineNumber > 0) {
				SendEditor(SCI_GOTOLINE, lineNumber);
			}
		}
	} else {
		WarnUser(warnWrongFile);
	}
}

void SciTEBase::Revert() {
	OpenFile(false);
}

void SciTEBase::CheckReload() {
	if (props.GetInt("load.on.activate")) {
		// Make a copy of fullPath as otherwise it gets aliased in Open
		char fullPathToCheck[MAX_PATH];
		strcpy(fullPathToCheck, fullPath);
		time_t newModTime = GetModTime(fullPathToCheck);
		//Platform::DebugPrintf("Times are %d %d\n", fileModTime, newModTime);
		if (newModTime > fileModTime) {
			if (isDirty) {
				static bool entered = false;   	// Stop reentrancy
				if (!entered && (0 == dialogsOnScreen)) {
					entered = true;
					char msg[MAX_PATH + 100];
					strcpy(msg, "The file \"");
					strcat(msg, fullPathToCheck);
					strcat(msg, "\" has been modified. Should it be reloaded?");
					dialogsOnScreen++;
					int decision = MessageBox(wSciTE.GetID(), msg, appName, MB_YESNO);
					dialogsOnScreen--;
					if (decision == IDYES) {
						Open(fullPathToCheck);
					}
					entered = false;
		}
	} else {
				Open(fullPathToCheck);
			}
		}
	}
}

void SciTEBase::Activate(bool activeApp) {
	if (activeApp) {
		CheckReload();
	} else {
		if (isDirty) {
			if (props.GetInt("save.on.deactivate") && fileName[0]) {
				// Trying to save at deactivation when no file name -> dialogs
				Save();
			}
		}
	}
}

int SciTEBase::SaveIfUnsure(bool forceQuestion) {
	if (isDirty) {
		if (props.GetInt("are.you.sure", 1) ||
		        IsUntitledFileName(fullPath) ||
		        forceQuestion) {
			char msg[MAX_PATH + 100];
			if (fileName[0]) {
				strcpy(msg, "Save changes to \"");
				strcat(msg, fullPath);
				strcat(msg, "\"?");
			} else {
				strcpy(msg, "Save changes to (Untitled)?");
			}
			dialogsOnScreen++;
			int decision = MessageBox(wSciTE.GetID(), msg, appName, MB_YESNOCANCEL);
			dialogsOnScreen--;
			if (decision == IDYES) {
				if (!Save())
					decision = IDCANCEL;
			}
			return decision;
		} else {
			if (!Save())
				return IDCANCEL;
		}
	}
	return IDYES;
}

int SciTEBase::SaveIfUnsureAll(bool forceQuestion) {
	UpdateBuffersCurrent();	// Ensure isDirty copied
	for (int i = 0; i < buffers.length; i++) {
		if (buffers.buffers[i].isDirty) {
			SetDocumentAt(i);
			if (SaveIfUnsure(forceQuestion) == IDCANCEL)
				return IDCANCEL;
		}
	}
	if (props.GetInt("save.recent", 0))
		SaveRecentStack();
	// Definitely going to exit now, so delete all documents
	// Set editor back to initial document
	SendEditor(SCI_SETDOCPOINTER, 0, buffers.buffers[0].doc);
	// Release all the extra documents
	for (int j = 0; j < buffers.size; j++) {
		if (buffers.buffers[j].doc)
			SendEditor(SCI_RELEASEDOCUMENT, 0, buffers.buffers[j].doc);
	}
	// Initial document will be deleted when editor deleted
	return IDYES;
}

int SciTEBase::SaveIfUnsureForBuilt() {
	if (isDirty) {
		if (props.GetInt("are.you.sure.for.build", 0))
			return SaveIfUnsure(true);

		Save();
	}
	return IDYES;
}

int StripTrailingSpaces(char *data, int ds, bool lastBlock) {
	int lastRead = 0;
	char *w = data;

	for (int i = 0; i < ds; i++) {
		char ch = data[i];
		if ((ch == ' ') || (ch == '\t')) {
			// Swallow those spaces
		}
		else if ((ch == '\r') || (ch == '\n')) {
			*w++ = ch;
			lastRead = i + 1;
		} else {
			while (lastRead < i) {
				*w++ = data[lastRead++];
			}
			*w++ = ch;
			lastRead = i + 1;
		}
	}
	// If a non-final block, then preserve whitespace at end of block as it may be significant.
	if (!lastBlock) {
		while (lastRead < ds) {
			*w++ = data[lastRead++];
		}
	}
	return w - data;
}

// Returns false only if cancelled
bool SciTEBase::Save() {
	if (fileName[0]) {
		if (props.GetInt("save.deletes.first")) {
			unlink(fullPath);
		}

		//Platform::DebugPrintf("Saving <%s><%s>\n", fileName, fullPath);
		//DWORD dwStart = timeGetTime();
		FILE *fp = fopen(fullPath, fileWrite);
		if (fp) {
			char data[blockSize + 1];
			int lengthDoc = LengthDocument();
			for (int i = 0; i < lengthDoc; i += blockSize) {
				int grabSize = lengthDoc - i;
				if (grabSize > blockSize)
					grabSize = blockSize;
				GetRange(wEditor, i, i + grabSize, data);
				if (props.GetInt("strip.trailing.spaces"))
					grabSize = StripTrailingSpaces(
					               data, grabSize, grabSize != blockSize);
				fwrite(data, grabSize, 1, fp);
			}
			fclose(fp);

			//MoveFile(fullPath, fullPath);

			//DWORD dwEnd = timeGetTime();
			//Platform::DebugPrintf("Saved file=%d\n", dwEnd - dwStart);
			fileModTime = GetModTime(fullPath);
			SendEditor(SCI_SETSAVEPOINT);
#if 0
			char fileNameLowered[MAX_PATH];
			strcpy(fileNameLowered, fileName);
			lowerCaseString(fileNameLowered);
			if (0 != strstr(fileNameLowered, ".properties")) {
#else
			if (IsPropertiesFile(fileName)) {
#endif
				ReadGlobalPropFile();
				ReadLocalPropFile();
				ReadProperties();
				SetWindowName();
				BuffersMenu();
				Redraw();
			}
		} else {
			char msg[200];
			strcpy(msg, "Could not save file \"");
			strcat(msg, fullPath);
			strcat(msg, "\".");
			dialogsOnScreen++;
			MessageBox(wSciTE.GetID(), msg, appName, MB_OK);
			dialogsOnScreen--;
		}
		return true;
	} else {
		return SaveAs();
	}
}

bool SciTEBase::SaveAs(char *file) {
	if (file && *file) {
		SetFileName(file);
		Save();
		ReadProperties();
		SendEditor(SCI_CLEARDOCUMENTSTYLE);
		SendEditor(SCI_COLOURISE, 0, -1);
		Redraw();
		SetWindowName();
		BuffersMenu();
		return true;
	} else {
		return SaveAsDialog();
	}
}
