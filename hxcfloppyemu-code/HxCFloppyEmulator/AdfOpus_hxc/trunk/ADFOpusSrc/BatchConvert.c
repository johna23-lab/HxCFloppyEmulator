/* ADF Opus Copyright 1998-2002 by 
 * Dan Sutherland <dan@chromerhino.demon.co.uk> and Gary Harris <gharris@zip.com.au>.	
 *
 */
/*! \file BatchConvert.c
 *  \brief Batch Converter functions.
 *
 * BatchConvert.c - routines to handle the batch converter dialogue
 */

#include "Pch.h"

#include "ADFOpus.h"
#include "Xdms.h"
#include "Utils.h"
#include "BatchConvert.h"
#include "zLib.h"
#include "Help\AdfOpusHlp.h"
#include "libhxcfe.h"

#define BUFLEN      16384

extern HANDLE ghInstance;
char   strOFNFileNames[MAX_PATH * 10];
extern HWND ghwndFrame;


//extern void dmsErrMsg(USHORT, char *, char *, char *);

LRESULT CALLBACK BatchConvertProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
	static DWORD aIds[] = { 
		IDC_BCFILELIST,			IDH_BATCH_SOURCE_LIST,
		IDC_BCADD,				IDH_BATCH_ADD_BUTTON,	
		IDC_BCREMOVE,			IDH_BATCH_REMOVE_BUTTON,	
		IDC_BCREMOVEALL,		IDH_BATCH_REMOVE_ALL_BUTTON,
		IDC_BCDELETE_ORIGINAL,	IDH_BATCH_DELETE_ORIGINAL,
		IDC_BCADF,				IDH_BATCH_ADF_BUTTON,
		IDC_BCADZ,				IDH_BATCH_ADZ_BUTTON,
		IDC_BCSTATUS,			IDH_BATCH_STATUS,	
		IDC_BCSTART,			IDH_BATCH_START_BUTTON,
		IDC_BCHELP,				IDH_BATCH_HELP_BUTTON,
		IDCANCEL,				IDH_BATCH_CLOSE_BUTTON,
		0,0 
	}; 	

	
	switch(msg) {
	case WM_INITDIALOG:
		SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_SETCHECK, BST_CHECKED, 0l);
		BCUpdateButtons(dlg);
		return TRUE;
	case WM_COMMAND:
		switch((int)LOWORD(wp)) {
		case IDC_BCADD:
			BCAddFiles(dlg);
			return TRUE;
		case IDC_BCSTART:
			BCConvert(dlg);
			return TRUE;
		case IDC_BCFILELIST:
			BCUpdateButtons(dlg);
			return TRUE;
		case IDC_BCREMOVE:
			BCDeleteSelected(dlg);
			return TRUE;
		case IDC_BCREMOVEALL:
			BCDeleteAll(dlg);
			// Re-enable output buttons if all names are removed.
			SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_SETCHECK, BST_CHECKED, 0);
			SendMessage(GetDlgItem(dlg, IDC_BCADZ), BM_SETCHECK, BST_UNCHECKED, 0);
			EnableWindow(GetDlgItem(dlg, IDC_BCADF), TRUE);
			EnableWindow(GetDlgItem(dlg, IDC_BCADZ), TRUE);

			return TRUE;

		case IDC_BCHELP:
			// Implement help button.
			WinHelp(dlg, "ADFOpus.hlp>Opus_win", HELP_CONTEXT, IDH_BATCH_CONVERTER);
			return TRUE;

		case IDCANCEL:
			EndDialog(dlg, TRUE);
			iFileTypeSelected = 0;			// Reset file type so output buttons are reset.
			return TRUE;
		}
		break;
	case WM_CLOSE:
		EndDialog(dlg, TRUE);
		iFileTypeSelected = 0;				// Reset file type so output buttons are reset.
		return TRUE;

	// Context sensitive help.
    case WM_HELP: 
        WinHelp(((LPHELPINFO) lp)->hItemHandle, "adfopus.hlp", 
			HELP_WM_HELP, (DWORD) (LPSTR) aIds); 
        break; 
 
    case WM_CONTEXTMENU: 
        WinHelp((HWND) wp, "adfopus.hlp", HELP_CONTEXTMENU, 
            (DWORD) (LPVOID) aIds); 
        break; 	

	}
	return FALSE;
}

void BCAddFiles(HWND dlg)
// Open files dialogue.
{
	OPENFILENAME ofn;
	HWND lb = GetDlgItem(dlg, IDC_BCFILELIST);
	int i, no;
	char ts[MAX_PATH];

	strcpy(strOFNFileNames, "");
	// Set up OPENFILENAME structure.
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = dlg;
	ofn.hInstance = NULL;
	// Filter indices are adf:1, dms:2, adz:3.
	ofn.lpstrFilter = "Amiga Disk Files (*.adf)\0*.adf\0Compressed Disk Files (*.adz)\0*.adz\0Diskmasher Files (*.dms)\0*.dms\0HxC Floppy Emulator HFE Files (*.hfe)\0*.hfe\0\0";
	ofn.lpstrCustomFilter = NULL;
	ofn.nMaxCustFilter = 0;
	ofn.lpstrFile = strOFNFileNames;
	ofn.nMaxFile = sizeof(strOFNFileNames);
	ofn.lpstrFileTitle = NULL;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrTitle = "Open disk image";
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_SHAREAWARE |
		OFN_ALLOWMULTISELECT | OFN_HIDEREADONLY | OFN_EXPLORER;
	ofn.lpstrDefExt = NULL;

	if (! GetOpenFileName(&ofn))
		return;

	no = CountOFNFiles(strOFNFileNames, sizeof(strOFNFileNames));
	if (no == 1)
		SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)&strOFNFileNames);
	else
		for (i = 1 ; i < no ; i++) {
			GetOFNFile(strOFNFileNames, i, ts);
			SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)ts);
		}

	iFileTypeSelected = ofn.nFilterIndex;		// Get the index of the selected filter.

	BCUpdateButtons(dlg);						// Dis/enable buttons.
}

void BCConvert(HWND dlg)
// performs the batch conversion on selected files
// TODO: progress dialogue, option to cancel process
//		see SHFileOperation for recycle bin ops, write seperate fn.
{
	int		count;
	int		i, j, iLength;
	char	inBuf[MAX_PATH];
	char	outBuf[MAX_PATH];
	char	statusBuf[MAX_PATH];
	char	FileRoot[MAX_PATH];
	char	FileSuf[4];
	char	fileName[MAX_PATH];
	HWND	fl = GetDlgItem(dlg, IDC_BCFILELIST);
	HWND	sl = GetDlgItem(dlg, IDC_BCSTATUS);
	
	int side,track,nbsect,image_size;
	HXCFE * hxcfe;
	HXCFE_FLOPPY* fp;
	HXCFE_SECTORACCESS* ss;
	HXCFE_IMGLDR * imgldr_ctx;
	int loaderId;
	unsigned char * floppybuffer;
	FILE * f;

	LRESULT	State_ADZ;
	LRESULT	State_ADF;
	LRESULT	State_HFE;

	HANDLE	hFile;
	BOOL	bUsingTemp = FALSE;		// TRUE if using temp directory for intermediate file.
	USHORT	dmsError;
//	char	dmsErrorMessage[MAX_PATH];
	BOOL	bOverwriting = TRUE;


	count = SendMessage(fl, LB_GETCOUNT, 0, 0l);
	
	for (i = 0 ; i < count ; i++) {
		SendMessage(fl, LB_GETTEXT, 0, (LPARAM)&inBuf);		// Get filename from list.

		UpdateWindow(dlg);

		strcpy(outBuf, inBuf);

		iLength = strlen(inBuf);						// Get name length.
		for(j = 0;j < iLength - 3;j++)					// Get name root.
			FileRoot[j] = inBuf[j];
		FileRoot[j] = '\0';

		FileSuf[0] = inBuf[iLength - 3];				// Get name suffix.
		FileSuf[1] = inBuf[iLength - 2];
		FileSuf[2] = inBuf[iLength - 1];
		FileSuf[3] = '\0';
		
		strcpy(outBuf, FileRoot);						// Set the outfile name root.
	
		// Get the check state of the output buttons.
		State_ADZ = SendMessage(GetDlgItem(dlg, IDC_BCADZ), BM_GETSTATE, 0, 0);
		State_ADF = SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_GETSTATE, 0, 0);
		State_HFE = SendMessage(GetDlgItem(dlg, IDC_BCHFE), BM_GETSTATE, 0, 0);

		// if hfe.
		if(strcmp(FileSuf, "hfe") == 0 || strcmp(FileSuf, "HFE") == 0 ){
			// Print decompression status message.
			sprintf(statusBuf, "Unpacking file '%s'...", inBuf);	

			// If dms to adz, use temp directory.
			if(State_ADZ & BST_CHECKED){
				_splitpath(strOFNFileNames, NULL, NULL, fileName, NULL);	// Get filename.
				strcpy(outBuf, dirTemp);
				strcat(outBuf, fileName);
				strcat(outBuf, ".");
				bUsingTemp = TRUE;						// Using temp dir.
			}
			
			strcat(outBuf, "adf");						// Set the outfile name suffix.
			SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);

			// Check for file overwrite.
			hFile = CreateFile(outBuf, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			// If file exists and we're not writing to temp dir, ask.
			if(hFile != INVALID_HANDLE_VALUE && !bUsingTemp){
				sprintf(statusBuf, "The file %s already exists.\n Do you want to overwrite this file?", outBuf);
				// Set bool if not overwriting.
				if(MessageBox(dlg, statusBuf, "ADF Opus Warning", MB_YESNO|MB_ICONEXCLAMATION) == IDNO){
					bOverwriting = FALSE;
				}
			}

			if(bOverwriting){
			// Overwrite.
				// Open HFE
				hxcfe =  hxcfe_init();
				imgldr_ctx = hxcfe_imgInitLoader(hxcfe);
				loaderId = hxcfe_imgGetLoaderID(imgldr_ctx,"HXC_HFE");
				fp = hxcfe_imgLoad(imgldr_ctx,(char*)inBuf,loaderId,0);
				if(fp)
				{
					image_size = hxcfe_getFloppySize(hxcfe,fp,0);

					if(image_size)
					{
						floppybuffer=(char*)malloc(image_size);

						nbsect=11;
						switch(image_size)
						{
							case 80*11*2*512:
								nbsect=11;
							break;
							case 80*22*2*512:
								nbsect=22;
							break;
						}

						ss = hxcfe_initSectorAccess(hxcfe,fp);

						for(track=0;track<hxcfe_getNumberOfTrack(hxcfe,fp);track++)
						{
							for(side=0;side<hxcfe_getNumberOfSide(hxcfe,fp);side++)
							{
								hxcfe_readSectorData(ss,track,side,0,nbsect,512,AMIGA_MFM_ENCODING,&floppybuffer[(512*nbsect)*((track*hxcfe_getNumberOfSide(hxcfe,fp))+side)],0);
							}
						}
							
						hxcfe_deinitSectorAccess(ss);

						hxcfe_imgUnload(imgldr_ctx,fp);

						hxcfe_imgDeInitLoader(imgldr_ctx);
						hxcfe_deinit(hxcfe);
							
						f=fopen(outBuf,"wb");
						if(f)
						{
							fwrite(floppybuffer,image_size,1,f);
							fclose(f);
						}
						
						free(floppybuffer);

						strcpy(statusBuf, "...file loaded successfully.");
					}

				}
				else
				{
					hxcfe_deinit(hxcfe);
					strcpy(statusBuf, "...error occured during HFE Loading.");
				}
			}
			else{
				// Abort
				strcpy(statusBuf, "...aborted to avoid overwrite.");
				SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);
			}
		}
		
		// if dms.				 
		if(strcmp(FileSuf, "dms") == 0 || strcmp(FileSuf, "DMS") == 0 ){
			// Print decompression status message.
			sprintf(statusBuf, "Unpacking file '%s'...", inBuf);	

			// If dms to adz, use temp directory.
			if(State_ADZ & BST_CHECKED){
				_splitpath(strOFNFileNames, NULL, NULL, fileName, NULL);	// Get filename.
				strcpy(outBuf, dirTemp);
				strcat(outBuf, fileName);
				strcat(outBuf, ".");
				bUsingTemp = TRUE;						// Using temp dir.
			}
			
			strcat(outBuf, "adf");						// Set the outfile name suffix.
			SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);

			// Check for file overwrite.
			hFile = CreateFile(outBuf, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			// If file exists and we're not writing to temp dir, ask.
			if(hFile != INVALID_HANDLE_VALUE && !bUsingTemp){
				sprintf(statusBuf, "The file %s already exists.\n Do you want to overwrite this file?", outBuf);
				// Set bool if not overwriting.
				if(MessageBox(dlg, statusBuf, "ADF Opus Warning", MB_YESNO|MB_ICONEXCLAMATION) == IDNO){
					bOverwriting = FALSE;
				}
			}

			if(bOverwriting){
			// Overwrite.
				if(dmsError = dmsUnpack(inBuf, outBuf) == NO_PROBLEM)	// Decompress.
					strcpy(statusBuf, "...file unpacked successfully.");
				else
					strcpy(statusBuf, "...error occured during DMS decompression.");
			}
			else{
				// Abort
				strcpy(statusBuf, "...aborted to avoid overwrite.");

// ************************** TRYING TO ACCESS DMS ERRORS IN XDMS.C - WON'T COMPILE
//				dmsErrMsg(dmsError, inBuf, outBuf, dmsErrorMessage);
//				strcat(statusBuf, dmsErrorMessage);
				SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);
			}
		}
		

		//if adz and adf button selected.
		if((strcmp(FileSuf, "adz") == 0 || strcmp(FileSuf, "ADZ") == 0) && (State_ADF & BST_CHECKED) ){
			// Print decompression status message.
			sprintf(statusBuf, "Unpacking file '%s'...", inBuf);	
			strcat(outBuf, "adf");						// Set the outfile name suffix.
			SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);

			if(GZDecompress(dlg, inBuf, outBuf))
				strcpy(statusBuf, "...file unpacked successfully.");
			else
				strcpy(statusBuf, "...failed to unpack file.");
		}

		//if adf or decompressed dms and adz button selected.
		if(
			(   ( strcmp(FileSuf, "adf") == 0 || strcmp(FileSuf, "ADF") == 0 ) 
			||  ( strcmp(FileSuf, "dms") == 0 || strcmp(FileSuf, "DMS") == 0 ) ) 
			&&  (State_ADZ & BST_CHECKED) )
		{
			
			// If recompressing an adf'd dms, remove the suffixes and
			// write last status message.
			if(strcmp(FileSuf, "dms") == 0 || strcmp(FileSuf, "DMS") == 0){
				SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);	
				strcpy(inBuf, outBuf);
				strcpy(outBuf, FileRoot);
			}

			// Print compression status message.
			sprintf(statusBuf, "Compressing file '%s'...", inBuf);	

			strcat(outBuf, "adz");						// Set the outfile name suffix.
			SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);

			if(GZCompress(dlg, inBuf, outBuf))
				strcpy(statusBuf, "...file compressed successfully.");
			else
				strcpy(statusBuf, "...failed to compress file.");

			// Delete intermediate adf.
			if(strcmp(FileSuf, "dms") == 0 || strcmp(FileSuf, "DMS") == 0)
				remove(inBuf);
		}


		// hfe button selected.
		if(  ( ( strcmp(FileSuf, "adf") == 0 || strcmp(FileSuf, "ADF") == 0 )
			|| ( strcmp(FileSuf, "dms") == 0 || strcmp(FileSuf, "DMS") == 0 ) 
			|| ( strcmp(FileSuf, "adz") == 0 || strcmp(FileSuf, "ADZ") == 0 ) )
			&& (State_HFE & BST_CHECKED) ){
			
			// If recompressing an adf'd dms, remove the suffixes and
			// write last status message.
			if(strcmp(FileSuf, "dms") == 0 || strcmp(FileSuf, "DMS") == 0){
				SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);	
				strcpy(inBuf, outBuf);
				strcpy(outBuf, FileRoot);
			}

			// Print compression status message.
			sprintf(statusBuf, "Compressing file '%s'...", inBuf);	

			strcat(outBuf, "hfe");						// Set the outfile name suffix.
			SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);

			hxcfe=hxcfe_init();
			imgldr_ctx = hxcfe_imgInitLoader(hxcfe);

			fp=0;
			loaderId = hxcfe_imgAutoSetectLoader(imgldr_ctx,inBuf,0);
			// Load the image
			if(loaderId>=0)
				fp = hxcfe_imgLoad(imgldr_ctx,inBuf,loaderId,0);
			if(fp)
			{
				// Select the HFE loader/exporter.
				loaderId=hxcfe_imgGetLoaderID(imgldr_ctx,"HXC_HFE");
				// Save the file...
				hxcfe_imgExport(imgldr_ctx,fp,outBuf,loaderId);
				// Free the loaded image
				hxcfe_imgUnload(imgldr_ctx,fp);
				strcpy(statusBuf, "...file compressed successfully.");
			}
			else
			{
				strcpy(statusBuf, "...failed to compress file.");
			}

			hxcfe_imgDeInitLoader(imgldr_ctx);
			hxcfe_deinit(hxcfe);

			// Delete intermediate adf.
			if(strcmp(FileSuf, "dms") == 0 || strcmp(FileSuf, "DMS") == 0)
				remove(inBuf);
		}
		//no dms compression at this stage.

		SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);		// Write final status message.
		SendMessage(fl, LB_DELETESTRING, 0, 0l);					// Delete file from lister. 
		bUsingTemp = FALSE;											// Reset temp dir flag.

		if(SendDlgItemMessage(dlg, IDC_BCDELETE_ORIGINAL, BM_GETCHECK, 0, 0L) == BST_CHECKED){
		//Delete the original file.
			remove(inBuf);
			sprintf(statusBuf, "Deleted %s.", inBuf);
			SendMessage(sl, LB_ADDSTRING, 0, (LPARAM)&statusBuf);	// Write deletion message.
		}
	}
	// Write a divider line to improve readability.
	SendMessage(sl, LB_ADDSTRING, 0, 
		(LPARAM)"----------------------------------------------------------------------------------------------");

	// Re-enable output buttons once operations are complete.
	SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_SETCHECK, BST_CHECKED, 0);
	SendMessage(GetDlgItem(dlg, IDC_BCADZ), BM_SETCHECK, BST_UNCHECKED, 0);
	SendMessage(GetDlgItem(dlg, IDC_BCHFE), BM_SETCHECK, BST_UNCHECKED, 0);
	EnableWindow(GetDlgItem(dlg, IDC_BCADF), TRUE);
	EnableWindow(GetDlgItem(dlg, IDC_BCADZ), TRUE);
	EnableWindow(GetDlgItem(dlg, IDC_BCHFE), TRUE);

	BCUpdateButtons(dlg);						// Dis/enable buttons.
}

void BCUpdateButtons(HWND dlg)
/* enable/disable relevant list buttons depending on item count and selection
 */
{
	int		count;

	count = SendMessage(GetDlgItem(dlg, IDC_BCFILELIST), LB_GETCOUNT, 0, 0l);

	EnableWindow(GetDlgItem(dlg, IDC_BCSTART), count);
	EnableWindow(GetDlgItem(dlg, IDC_BCREMOVEALL), count);

	count = SendMessage(GetDlgItem(dlg, IDC_BCFILELIST), LB_GETSELCOUNT, 0, 0l);
	EnableWindow(GetDlgItem(dlg, IDC_BCREMOVE), count);

	// If ADFs have been selected, disable the ADF output button
	if(iFileTypeSelected == 1){
		// ADF and ADZ buttons are checkboxes. Uncheck and check.
		SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCADZ), BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCHFE), BM_SETCHECK, BST_CHECKED, 0);
		EnableWindow(GetDlgItem(dlg, IDC_BCADZ), TRUE);
		EnableWindow(GetDlgItem(dlg, IDC_BCHFE), TRUE);
		EnableWindow(GetDlgItem(dlg, IDC_BCADF), FALSE);
	}
	// If ADZs have been selected, disable the ADZ output button.
	if(iFileTypeSelected == 2){
		SendMessage(GetDlgItem(dlg, IDC_BCADZ), BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCHFE), BM_SETCHECK, BST_CHECKED, 0);
		EnableWindow(GetDlgItem(dlg, IDC_BCADZ), FALSE);
		EnableWindow(GetDlgItem(dlg, IDC_BCHFE), TRUE);
		EnableWindow(GetDlgItem(dlg, IDC_BCADF), TRUE);
	}	

	// If DMSs have been selected
	if(iFileTypeSelected == 3){
		SendMessage(GetDlgItem(dlg, IDC_BCADZ), BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCHFE), BM_SETCHECK, BST_CHECKED, 0);
		EnableWindow(GetDlgItem(dlg, IDC_BCADZ), TRUE);
		EnableWindow(GetDlgItem(dlg, IDC_BCHFE), TRUE);
		EnableWindow(GetDlgItem(dlg, IDC_BCADF), TRUE);
	}

	// If HFEs have been selected
	if(iFileTypeSelected == 4){
		SendMessage(GetDlgItem(dlg, IDC_BCADZ), BM_SETCHECK, BST_UNCHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCADF), BM_SETCHECK, BST_CHECKED, 0);
		SendMessage(GetDlgItem(dlg, IDC_BCHFE), BM_SETCHECK, BST_UNCHECKED, 0);
		EnableWindow(GetDlgItem(dlg, IDC_BCADZ), TRUE);
		EnableWindow(GetDlgItem(dlg, IDC_BCHFE), FALSE);
		EnableWindow(GetDlgItem(dlg, IDC_BCADF), TRUE);
	}

}

void BCDeleteSelected(HWND dlg)
{
	int i;
	HWND fl = GetDlgItem(dlg, IDC_BCFILELIST);

	for (i = SendMessage(fl, LB_GETCOUNT, 0, 0l) - 1 ; i >= 0 ; i--)
		if (SendMessage(fl, LB_GETSEL, i, 0l))
			SendMessage(fl, LB_DELETESTRING, i, 0l);

	BCUpdateButtons(dlg);
}

void BCDeleteAll(HWND dlg)
{
	int i;
	HWND fl = GetDlgItem(dlg, IDC_BCFILELIST);

	for (i = SendMessage(fl, LB_GETCOUNT, 0, 0l) - 1 ; i >= 0 ; i--)
		SendMessage(fl, LB_DELETESTRING, i, 0l);

	BCUpdateButtons(dlg);
}

BOOL GZCompress(HWND win, char *infile, char *outfile)
// Compress an adf with gzip.
// Input: input file name, output file name, handle to owner window.
// Send NULL HWND to prevent the overwrite check.
{
	char buf[BUFLEN];
	int len;
	FILE *in;
	gzFile out;

	if(win != NULL && fopen(outfile, "r")){
		sprintf(buf, "%s already exists.\n Do you want to overwrite this file?", outfile);
		if(MessageBox(win, buf, "ADF Opus Warning", MB_YESNO|MB_ICONEXCLAMATION) == IDNO)
			return FALSE;
    }

	in = fopen(infile, "rb");
    if (in == NULL)
		return FALSE;

    out = gzopen(outfile, "wb9");
    if (out == NULL)
		return FALSE;


	for (;;) {
		len = fread(buf, 1, sizeof(buf), in);
		if (ferror(in))
			return FALSE;
		if (len == 0) break;

		if (gzwrite(out, buf, (unsigned)len) != len)
			return FALSE;
	}
	fclose(in);
	if (gzclose(out) != Z_OK)
		return FALSE;

	return TRUE;
}


BOOL GZDecompress(HWND win, char *infile, char *outfile)
// Decompress an adf compressed with gzip.
// Input: input file name, output file name, handle to owner window.
// Send NULL HWND to prevent the overwrite check.
{
    gzFile	in;
    FILE	*out;
    char	buf[BUFLEN];
    int		len;

	if(win != NULL && fopen(outfile, "r")){
		sprintf(buf, "%s already exists.\n Do you want to overwrite this file?", outfile);
		if(MessageBox(win, buf, "ADF Opus Warning", MB_YESNO|MB_ICONEXCLAMATION) == IDNO)
			return FALSE;
    }

    out = fopen(outfile, "wb");
    if(out == NULL)
		return FALSE;

    in = gzopen(infile, "rb");
    if(in == NULL)
		return FALSE;

	for(;;){
        len = gzread(in, buf, sizeof(buf));
        if(len < 0)
			return FALSE;
        if(len == 0) 
			break;
        if((int)fwrite(buf, 1, (unsigned)len, out) != len)
			return FALSE;
    }
    if(fclose(out)) 
		return FALSE;

    if(gzclose(in) != Z_OK) 
		return FALSE;

	return TRUE;
}

