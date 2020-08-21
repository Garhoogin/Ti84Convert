#ifdef UNICODE
#	undef UNICODE
#endif

#include <commdlg.h>
#include <Windows.h>

#pragma pack(1)

/**
 * structure for holding a picture entry
 */
typedef struct ENTRY_{
	WORD w1; //0x000D
	WORD entrySize;
	BYTE type;
	CHAR name[8];
	BYTE version; //0
	BYTE flag;
	WORD entrySize2;
	WORD picSize;
	BYTE pixDat[756];
} ENTRY;

/**
 * structure for holding a variable entry encapsulating a picture
 */
typedef struct VAR_{
	CHAR signature[8];
	CHAR signature2[3];
	CHAR comment[42];
	WORD dataLength;
	ENTRY entry;
	WORD checksum;
} VAR;

/**
 * Some imports from GDI+
 */
extern INT __stdcall GdiplusStartup(LPVOID n, LPVOID n2, INT n3);
extern INT __stdcall GdipCreateBitmapFromFile(LPWSTR str, LPVOID bmp);
extern INT __stdcall GdipGetImageWidth(LPVOID img, PINT width);
extern INT __stdcall GdipGetImageHeight(LPVOID img, PINT width);
extern INT __stdcall GdipBitmapLockBits(LPVOID img, LPRECT rc, INT n1, INT n2, LPVOID n3);
extern INT __stdcall GdipDisposeImage(LPVOID img);


/**
 * a really horrible way to use GDI+.
 */
typedef struct {
	DWORD d1[6];
} BITMAPDATA;

typedef struct {
	DWORD GdiplusVersion;
	DWORD DebugEventCallback;
	BOOL SuppressBackgroundThread;
	BOOL SuppressExternalCodecs;
} STARTUPINPUT;

HANDLE heap = NULL;
HANDLE g_hInstance;

LPSTR saveFileDialog(HWND hWnd, LPSTR title, LPSTR filter, LPSTR extension) {
	OPENFILENAMEA o = { 0 };
	CHAR fbuff[MAX_PATH + 1] = { 0 };
	o.lStructSize = sizeof(o);
	o.hwndOwner = hWnd;
	o.nMaxFile = MAX_PATH;
	o.lpstrTitle = title;
	o.lpstrFilter = filter;
	o.nMaxCustFilter = 255;
	o.lpstrFile = fbuff;
	o.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	o.lpstrDefExt = extension;

	if (GetSaveFileNameA(&o)) {

		LPSTR fname = (LPSTR) HeapAlloc(heap, HEAP_ZERO_MEMORY, strlen(fbuff) + 1, 2);
		memcpy(fname, fbuff, strlen(fbuff));
		return fname;
	}
	return NULL;
}

LPSTR openFileDialog(HWND hWnd, LPSTR title, LPSTR filter, LPSTR extension) {
	OPENFILENAMEA o = { 0 };
	CHAR fname[MAX_PATH + 1] = { 0 };
	o.lStructSize = sizeof(o);
	o.hwndOwner = hWnd;
	o.nMaxFile = MAX_PATH;
	o.lpstrTitle = title;
	o.lpstrFilter = filter;
	o.nMaxCustFilter = 255;
	o.lpstrFile = fname;
	o.Flags = OFN_ENABLESIZING | OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	o.lpstrDefExt = extension;
	if (GetOpenFileNameA(&o)) {
		LPSTR fname2 = (LPSTR) HeapAlloc(heap, HEAP_ZERO_MEMORY, strlen(fname) + 1, 2);
		memcpy(fname2, fname, strlen(fname));
		return fname2;
	}
	return NULL;
}

/**
 * use GDI+ to read an image file and return an array of pixels.
 */
LPDWORD readbmpnew(LPSTR path, PINT pWidth, PINT pHeight) {
	int i;
	RECT r;
	r.left = 0;
	r.top = 0;
	BYTE * gp = NULL;
	BITMAPDATA bm;

	WCHAR filename[MAX_PATH];
	i = 0;
	while (*path) {
		filename[i] = (WCHAR) *path;
		path++, i++;
	}
	filename[i] = 0;

	if (GdipCreateBitmapFromFile(filename, (void *) &gp)) {
		return NULL;
	}
	GdipGetImageWidth(gp, (PINT) &(r.right));
	GdipGetImageHeight(gp, (PINT) &(r.bottom));
	GdipBitmapLockBits(gp, (void *) &r, 3, 0x0026200A, (void *) &bm);

	LPDWORD px = (LPDWORD) HeapAlloc(heap, HEAP_ZERO_MEMORY, r.right * r.bottom * 4);
	*pWidth = r.right;
	*pHeight = r.bottom;
	DWORD * scan0 = (DWORD *) bm.d1[4];
	for (i = 0; i < r.right * r.bottom; i++) {
		DWORD d = scan0[i];
		d = ((d & 0xFF) << 16) | (d & 0xFF00FF00) | ((d >> 16) & 0xFF);
		px[i] = d;
	}
	GdipDisposeImage(gp);
	return px;
}


/**
 * sample one or more pixels from the input image, based on
 * position and sample box size.
 */
DWORD getColor(LPDWORD pixBuff, float x, float y, int width, int height, float boxWidth, float boxHeight){
	float x1 = x;
	float x2 = x + boxWidth;
	float y1 = y;
	float y2 = y + boxHeight;
	/* Calculate area */
	float area = boxWidth * boxHeight;
	/* begin making a weighted total. */
	float totalR = 0.0f, totalG = 0.0f, totalB = 0.0f;
	/* find the sampling box's actual size. */
	int rightmost = (int) x2;
	int leftmost = (int) x1;
	int topmost = (int) y1;
	int bottommost = (int) y2;
	int sampleWidth = rightmost - leftmost + 1;
	int sampleHeight = bottommost - topmost + 1;
	for(int i = 0; i < sampleHeight; i++){
		int ySample = i + topmost;
		for(int j = 0; j < sampleWidth; j++){
			int xSample = j + leftmost;
			if(xSample >= width || ySample >= height) continue;
			DWORD color = pixBuff[xSample + ySample * width];
			int r = color & 0xFF;
			int g = (color >> 8) & 0xFF;
			int b = (color >> 16) & 0xFF;
			/* find weight. Equal to the pixel's coverage by the box. */
			float weight = 1.0f;
			if(!i || !j || (i == sampleHeight - 1) || (j == sampleWidth - 1)){
				/* an edge case. The area must be actually calculated. */
				float pixWidth = 1.0f, pixHeight = 1.0f;
				if(!i) pixHeight -= y1 - topmost;
				if(i == sampleHeight - 1) pixHeight -= 1 + bottommost - y2;
				if(!j) pixWidth -= x1 - leftmost;
				if(j == sampleWidth - 1) pixWidth -= 1 + rightmost - x2;
				if(pixWidth < 0) pixWidth = 0;
				if(pixHeight < 0) pixHeight = 0;
				weight = pixWidth * pixHeight;
			}
			totalR += ((float) r) * weight;
			totalG += ((float) g) * weight;
			totalB += ((float) b) * weight;
		}
	}
	totalR = totalR / area + 0.5f;
	totalG = totalG / area + 0.5f;
	totalB = totalB / area + 0.5f;
	if(totalR > 255.0f) totalR = 255.0f;
	if(totalG > 255.0f) totalG = 255.0f;
	if(totalB > 255.0f) totalB = 255.0f;
	return ((int) totalR) | (((int) totalG) << 8) | (((int) totalB) << 16);
}


/**
 * read a bitmap and resize it to 96x63.
 */
LPDWORD readbmp(LPSTR path){
	int width, height, x, y;
	LPDWORD px = readbmpnew(path, &width, &height);
	if(!px) return NULL;
	if(width != 96 || height != 63){
		LPDWORD actuals = (LPDWORD) HeapAlloc(heap, 0, 96 * 63 * 4);
		float boxWidth = ((float) width) / 96.0f;
		float boxHeight = ((float) height) / 63.0f;
		for(x = 0; x < 96; x++){
			for(y = 0; y < 63; y++){
				float mapX = ((float) x) * ((float) width) / 96.0f;
				float mapY = ((float) y) * ((float) height) / 63.0f;
				actuals[x + y * 96] = getColor(px, mapX, mapY, width, height, boxWidth, boxHeight);
			}
		}
		HeapFree(heap, 0, px);
		return actuals;
	}
	return px;
}


/**
 * take an input bitmap and convert to black and white.
 */
void generate(LPDWORD out, LPDWORD in, int shadeMin, int shadeMax, float diffuse){
	/* create float array. For extra precision, calculations will be done in floating points. */
	LPDWORD bits = in;
	float range = ((float) (shadeMax - shadeMin)) / 255.0f;
	float * shades = (float *) out;
	for(int y = 0; y < 63; y++){
		for(int x = 0; x < 96; x++){
			int offs = y * 96 + x;
			DWORD px = bits[offs];
			int r = (px >> 16) & 0xFF;
			int g = (px >> 8) & 0xFF;
			int b = px & 0xFF;
			float v = 0.299f * ((float) r) + 0.587f * ((float) g) + 0.114f * ((float) b);
			v = range * v + shadeMin;
			shades[offs] = v;
		}
	}
	/* match to white or black, diffusing the result. */
	for(int y = 0; y < 63; y++){
		for(int x = 0; x < 96; x++){
			int offs = y * 96 + x;
			float flt = shades[offs];
			float chosen = (flt < 127.5f)? 0.0f: 255.0f;
			float diff = flt - chosen;
			shades[offs] = chosen;
			if(x > 0 && y < (63 - 1)){
				shades[offs + 96 - 1] += diff * 0.1875f * diffuse;
			}
			if(y < (63- 1)){
				shades[offs + 96] += diff * 0.3125f * diffuse;
			}
			if(x < (96 - 1) && y < (63 - 1)){
				shades[offs + 96 + 1] += diff * 0.0625f * diffuse;
			}
			if(x < (96 - 1)){
				shades[offs + 1] += diff * 0.4375f * diffuse;
			}
		}
	}
	/* map to binary values */
	LPDWORD finalbuff = (DWORD *) shades;
	for(int i = 0; i < 96 * 63; i++){
		float f = shades[i];
		int s = (int) f;
		if(s < 0) s = 0;
		if(s > 255) s = 255;
		DWORD d = s | (s << 8) | (s << 16);
		finalbuff[i] = d;
	}
}

/**
 * initialize a VAR struct
 */
void create(VAR *v){
	const char * cmt = "Created by TI Connect CE 5.3.0.384";
	char * comment = v->comment;
	int i;
	*(LPDWORD) (v->signature) = 0x49542A2A;
	*(LPDWORD) (v->signature + 4) = 0x2A463338;
	v->signature2[0] = 0x1A;
	v->signature2[1] = 0x0A;
	v->signature2[2] = 0x0A;
	for(i = 0; i < 10; i++) ((LPDWORD) comment)[i] = 0;
	*(LPDWORD) (comment + 40) = 0;
	for(i = 0; i < 8; i++) *(((LPDWORD) (v->comment)) + i) = *(((LPDWORD) cmt) + i);
	*(LPDWORD) (v->comment + 32) = *(LPDWORD) (cmt + 32);
	v->dataLength = 0x307;
	v->entry.w1 = 0xD;
	v->entry.entrySize = 0x2F6;
	v->entry.type = 0x7;
	*(LPDWORD) (v->entry.name) = 0;
	*(LPDWORD) (v->entry.name + 4) = 0;
	v->entry.name[0] = 0x60;
	v->entry.version = 0;
	v->entry.flag = 0;
	v->entry.entrySize2 = 0x2F6;
	v->entry.picSize = 0x2F4;
}

/**
 * compute the checksum of a VAR struct
 */
void checksum(VAR * v){
	BYTE * b = (BYTE *) &v->entry;
	WORD sum = 0;
	int i;
	for(i = 0; i < sizeof(ENTRY); i++){
		sum += b[i];
	}
	v->checksum = sum;
}

void imgwrite(LPSTR path, LPDWORD bw){
	VAR v;
	HANDLE hFile;
	DWORD dwWritten;
	int i, j;
	create(&v);
	for(i = 0; i < 756; i++){
		BYTE b = 0;
		int offs = i << 3;
		for(j = 0; j < 8; j++){
			BYTE val = (~bw[offs + j]) & 1;
			b = (b << 1) | val;
		}

		v.entry.pixDat[i] = b;
	}
	checksum(&v);
	hFile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	WriteFile(hFile, &v, sizeof(v), &dwWritten, NULL);
	CloseHandle(hFile);
}

/**
 * set the size of a window's client area
 */
VOID SetWindowSize(HWND hWnd, int width, int height) {
	RECT r;
	GetWindowRect(hWnd, &r);
	r.bottom = height + r.top;
	r.right = width + r.left;
	AdjustWindowRect(&r, GetWindowLongA(hWnd, GWL_STYLE), FALSE);
	MoveWindow(hWnd, r.left, r.top, r.right - r.left, r.bottom - r.top, FALSE);
}

DWORD * currentBits = NULL;
DWORD * currentCompressedBits = NULL;

HWND browseBtn, sliderMin, sliderMax, sliderDiffuse, saveBtn;

/* TCC doesn't create a string pool. I create one myself. */
/* Since switching to MSVC, this is kind of redundant. */
LPCSTR ctlStatic = "STATIC";
LPCSTR ctlTrackbar = "msctls_trackbar32";
LPCSTR ctlButton = "BUTTON";
LPCSTR ctlEdit = "EDIT";
LPCSTR trackbar = "trackbar";
LPCSTR clsName = "GraphicWindowClass";

LONG WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam){
	HINSTANCE hInstance = g_hInstance;
	switch(msg){
		case WM_CREATE:
		{
			currentBits = (LPDWORD) HeapAlloc(heap, 0, 96 * 63 * 4);
			currentCompressedBits = (LPDWORD) HeapAlloc(heap, 0, 96 * 63 * 4);
			for(int i = 0; i < 96 * 63; i++){
				currentBits[i] = 0x00FFFFFF;
				currentCompressedBits[i] = 0x00FFFFFF;
			}

			browseBtn = CreateWindowExA(0, ctlButton, "Select Input", WS_CHILD | WS_VISIBLE, 10, 87, 185, 22, hWnd, NULL, hInstance, NULL);
			saveBtn = CreateWindowExA(0, ctlButton, "Save", WS_CHILD | WS_VISIBLE, 205, 87, 185, 22, hWnd, NULL, hInstance, NULL);

			CreateWindowExA(0, ctlStatic, "Minimum:", WS_CHILD | WS_VISIBLE, 118, 10, 100, 22, hWnd, NULL, hInstance, NULL);
			CreateWindowExA(0, ctlStatic, "Maximum:", WS_CHILD | WS_VISIBLE, 118, 32, 100, 22, hWnd, NULL, hInstance, NULL);
			CreateWindowExA(0, ctlStatic, "Diffuse:", WS_CHILD | WS_VISIBLE, 118, 54, 100, 22, hWnd, NULL, hInstance, NULL);
			sliderMin = CreateWindowExA(0, ctlTrackbar, trackbar, WS_CHILD | WS_VISIBLE, 190, 10, 200, 22, hWnd, NULL, hInstance, NULL);
			sliderMax = CreateWindowExA(0, ctlTrackbar, trackbar, WS_CHILD | WS_VISIBLE, 190, 32, 200, 22, hWnd, NULL, hInstance, NULL);
			sliderDiffuse = CreateWindowExA(0, ctlTrackbar, trackbar, WS_CHILD | WS_VISIBLE, 190, 54, 200, 22, hWnd, NULL, hInstance, NULL);
#define TBM_SETPOS 0x405
#define TBM_SETRANGE 0x406
#define TBM_SETPAGESIZE 0x415
#define TBM_SETSEL 0x40A
#define TBM_GETPOS 0x400
			SendMessage(sliderMin, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG(0, 255));
			SendMessage(sliderMin, TBM_SETPAGESIZE, 0, (LPARAM) 4);
			SendMessage(sliderMin, TBM_SETSEL, (WPARAM) FALSE, (LPARAM) MAKELONG(0, 255));
			SendMessage(sliderMin, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) 0);

			SendMessage(sliderMax, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG(0, 255));
			SendMessage(sliderMax, TBM_SETPAGESIZE, 0, (LPARAM) 4);
			SendMessage(sliderMax, TBM_SETSEL, (WPARAM) FALSE, (LPARAM) MAKELONG(0, 255));
			SendMessage(sliderMax, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) 255);

			SendMessage(sliderDiffuse, TBM_SETRANGE, (WPARAM) TRUE, (LPARAM) MAKELONG(0, 255));
			SendMessage(sliderDiffuse, TBM_SETPAGESIZE, 0, (LPARAM) 4);
			SendMessage(sliderDiffuse, TBM_SETSEL, (WPARAM) FALSE, (LPARAM) MAKELONG(0, 255));
			SendMessage(sliderDiffuse, TBM_SETPOS, (WPARAM) TRUE, (LPARAM) 255);

			SetTimer(hWnd, 0, 16, NULL);
			SetWindowSize(hWnd, 400, 119);
			return 0;
		}
		case WM_TIMER:
		{
			generate(currentCompressedBits, currentBits, SendMessageA(sliderMin, TBM_GETPOS, 0, 0),
					 SendMessageA(sliderMax, TBM_GETPOS, 0, 0), ((float) SendMessageA(sliderDiffuse, TBM_GETPOS, 0, 0)) / 255.0f);
			InvalidateRect(hWnd, NULL, FALSE);
			return 0;
		}
		case WM_PAINT:
		{
			PAINTSTRUCT ps;
			HDC hDC = BeginPaint(hWnd, &ps);
			if(currentBits){
				/* draw bits */
				RECT r;
				r.top = 42 - 32;
				r.left = 10;
				r.right = 110;
				r.bottom = 109 - 32;
				DrawEdge(hDC, &r, EDGE_SUNKEN, BF_BOTTOM | BF_LEFT | BF_RIGHT | BF_TOP);
				HBITMAP hbm = CreateBitmap(96, 63, 1, 32, currentCompressedBits);
				HDC hDCTemp = CreateCompatibleDC(hDC);
				SelectObject(hDCTemp, hbm);
				BitBlt(hDC, 12, 44 - 32, 96, 63, hDCTemp, 0, 0, SRCCOPY);
				DeleteObject(hDCTemp);
				DeleteObject(hbm);
			}
			EndPaint(hWnd, &ps);
			return 0;
		}
		case WM_COMMAND:
		{
			if(HIWORD(wParam) == BN_CLICKED){
				HWND btn = (HWND) lParam;
				if(btn == browseBtn){
					LPSTR path = openFileDialog(hWnd, "Select Image", "All Files\0*.*\0\0", "");

					DWORD err;
					LPDWORD newBits = readbmp(path);
					if(newBits){
						if(currentBits) HeapFree(heap, 0, currentBits);
						if(currentCompressedBits) HeapFree(heap, 0, currentCompressedBits);
						currentBits = newBits;
						currentCompressedBits = HeapAlloc(heap, HEAP_ZERO_MEMORY, 96 * 63 * 4);
						generate(currentCompressedBits, currentBits, 0, 255, 1.0f);
					} else {
						err = GetLastError();
						//2 - not found
						LPSTR error = "Error";
						switch(err){
							case 2:
								MessageBoxA(hWnd, "File not found.", error, MB_ICONERROR);
								break;
							case 5:
							case 32:
								MessageBoxA(hWnd, "Access denied.", error, MB_ICONERROR);
								break;
							default:
								MessageBoxA(hWnd, "An unknown error occurred.", error, MB_ICONERROR);
						}
					}
					HeapFree(heap, 0, path);
				} else if(btn == saveBtn){
					LPSTR path = saveFileDialog(hWnd, "Save Location", "8xi files (*.8xi)\0*.8xi\0All Files\0*.*\0\0", ".8xi");
					imgwrite(path, currentCompressedBits);
					HeapFree(heap, 0, path);
				}
			}
			break;
		}
		case WM_DESTROY:
		{
			PostQuitMessage(0);
			break;
		}
	}
	return DefWindowProcA(hWnd, msg, wParam, lParam);
}

LRESULT WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow){
	STARTUPINPUT si;
	WNDCLASSEX wcex;
	ULONG_PTR token;
	HWND hWnd;
	MSG msg;
	int i;

	g_hInstance = hInstance;
	heap = GetProcessHeap();

	si.DebugEventCallback = 0;
	si.SuppressBackgroundThread = 0;
	si.SuppressExternalCodecs = 0;
	si.GdiplusVersion = 1;
	GdiplusStartup(&token, &si, 0);

	for(i = 0; i < 12; i++) ((LPDWORD) &wcex)[i] = 0;
	wcex.cbSize = sizeof(wcex);
	wcex.hInstance = hInstance;
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH) (COLOR_WINDOW);
	wcex.lpszClassName = clsName;
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	RegisterClassExA(&wcex);

	hWnd = CreateWindowExA(0, clsName, "TI-84 Picture Converter", WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 400, 300, NULL, NULL, hInstance, NULL);
	ShowWindow(hWnd, SW_SHOW);

	while(GetMessageA(&msg, NULL, 0, 0)){
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}

	return msg.wParam;

}

void _entry(void *peb){
	heap = *(HANDLE *) (((ULONG_PTR) peb) + 0x18);
	HMODULE base = *(HMODULE *) (((ULONG_PTR) peb) + 0x8);

	ExitProcess(WinMain(base, NULL, NULL, 0));
}
