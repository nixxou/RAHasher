/*
SevenZipArchive.cpp - drives 7z.dll through its COM-style interfaces to extract
archive entries into memory. The needed 7-Zip SDK interfaces are declared inline
here (matching CPP/7zip/IStream.h, IProgress.h, IPassword.h and
CPP/7zip/Archive/IArchive.h from the 7-Zip / 7-Zip-zstd source) so the project
keeps no source dependency on the SDK - only the runtime 7z.dll is required.
*/

#include "SevenZipArchive.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <propidl.h>
#include <oleauto.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "oleaut32.lib")

namespace
{
  /* ---- 7-Zip integer typedefs ------------------------------------------- */
  typedef unsigned char  Byte;
  typedef int32_t        Int32;
  typedef uint32_t       UInt32;
  typedef int64_t        Int64;
  typedef uint64_t       UInt64;

  /* ---- PropID values we read (CPP/7zip/PropID.h) ------------------------ */
  enum { kpidPath = 3, kpidIsDir = 6, kpidSize = 7, kpidCRC = 19 };

  /* ---- NAskMode / NOperationResult (CPP/7zip/Archive/IArchive.h) -------- */
  enum { kExtract = 0 };
  enum { kOpOK = 0 };

  /* ---- Interface IIDs ---------------------------------------------------
   * 7-Zip builds these as {23170F69-40C1-278A-0000-00<grp>00<sub>0000},
   * i.e. Data4 = {0,0,0,group,0,sub,0,0}. */
  #define DEF_7Z_IID(name, grp, sub) \
    const GUID name = {0x23170F69,0x40C1,0x278A,{0x00,0x00,0x00,(Byte)(grp),0x00,(Byte)(sub),0x00,0x00}}

  DEF_7Z_IID(IID_IProgress,                0, 0x05);
  DEF_7Z_IID(IID_ISequentialInStream,      3, 0x01);
  DEF_7Z_IID(IID_ISequentialOutStream,     3, 0x02);
  DEF_7Z_IID(IID_IInStream,                3, 0x03);
  DEF_7Z_IID(IID_IStreamGetSize,           3, 0x06);
  DEF_7Z_IID(IID_ICryptoGetTextPassword,   5, 0x10);
  DEF_7Z_IID(IID_IArchiveExtractCallback,  6, 0x20);
  DEF_7Z_IID(IID_IInArchive,               6, 0x60);
  #undef DEF_7Z_IID

  /* ---- Interfaces (method order/signatures must match the SDK exactly) -- */
  struct ISequentialInStream : public IUnknown
  {
    virtual HRESULT STDMETHODCALLTYPE Read(void* data, UInt32 size, UInt32* processedSize) = 0;
  };

  struct ISequentialOutStream : public IUnknown
  {
    virtual HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processedSize) = 0;
  };

  struct IInStream : public ISequentialInStream
  {
    virtual HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) = 0;
  };

  struct IStreamGetSize : public IUnknown
  {
    virtual HRESULT STDMETHODCALLTYPE GetSize(UInt64* size) = 0;
  };

  struct IProgress : public IUnknown
  {
    virtual HRESULT STDMETHODCALLTYPE SetTotal(UInt64 total) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64* completeValue) = 0;
  };

  struct IArchiveExtractCallback : public IProgress
  {
    virtual HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE PrepareOperation(Int32 askExtractMode) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 opRes) = 0;
  };

  struct ICryptoGetTextPassword : public IUnknown
  {
    virtual HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* password) = 0;
  };

  struct IArchiveOpenCallback; /* not needed - we pass NULL */

  struct IInArchive : public IUnknown
  {
    virtual HRESULT STDMETHODCALLTYPE Open(IInStream* stream, const UInt64* maxCheckStartPosition, IArchiveOpenCallback* openCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNumberOfItems(UInt32* numItems) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE Extract(const UInt32* indices, UInt32 numItems, Int32 testMode, IArchiveExtractCallback* extractCallback) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetArchiveProperty(PROPID propID, PROPVARIANT* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNumberOfProperties(UInt32* numProps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyInfo(UInt32 index, BSTR* name, PROPID* propID, VARTYPE* varType) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetNumberOfArchiveProperties(UInt32* numProps) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetArchivePropertyInfo(UInt32 index, BSTR* name, PROPID* propID, VARTYPE* varType) = 0;
  };

  typedef HRESULT(WINAPI* Func_CreateObject)(const GUID* clsID, const GUID* iid, void** outObject);

  /* ---- Format CLSIDs ----------------------------------------------------
   * {23170F69-40C1-278A-1000-000110<id>0000}, Data4 = {10,00,00,01,10,id,00,00} */
  GUID formatClsid(Byte id)
  {
    GUID g = {0x23170F69, 0x40C1, 0x278A, {0x10, 0x00, 0x00, 0x01, 0x10, id, 0x00, 0x00}};
    return g;
  }

  /* ---- small helpers ---------------------------------------------------- */
  std::wstring utf8ToWide(const std::string& s)
  {
    if (s.empty())
      return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
  }

  /* releases a PROPVARIANT we filled via GetProperty - only VT_BSTR allocates */
  void clearProp(PROPVARIANT& p)
  {
    if (p.vt == VT_BSTR && p.bstrVal)
      SysFreeString(p.bstrVal);
    memset(&p, 0, sizeof(p));
    p.vt = VT_EMPTY;
  }

  std::string wideToUtf8(const wchar_t* w)
  {
    if (!w || !*w)
      return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 1)
      return std::string();
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, NULL, NULL);
    return s;
  }

  /* ---- IInStream over a Win32 file -------------------------------------- */
  class CFileInStream : public IInStream, public IStreamGetSize
  {
    LONG _ref;
    HANDLE _file;
  public:
    CFileInStream() : _ref(1), _file(INVALID_HANDLE_VALUE) {}
    ~CFileInStream() { if (_file != INVALID_HANDLE_VALUE) CloseHandle(_file); }

    bool open(const std::string& path)
    {
      std::wstring wpath = utf8ToWide(path);
      _file = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      return _file != INVALID_HANDLE_VALUE;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
      if (!out) return E_POINTER;
      if (iid == IID_IUnknown)                 *out = static_cast<IUnknown*>(static_cast<IInStream*>(this));
      else if (iid == IID_ISequentialInStream) *out = static_cast<ISequentialInStream*>(this);
      else if (iid == IID_IInStream)           *out = static_cast<IInStream*>(this);
      else if (iid == IID_IStreamGetSize)      *out = static_cast<IStreamGetSize*>(this);
      else { *out = NULL; return E_NOINTERFACE; }
      AddRef();
      return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
      LONG r = InterlockedDecrement(&_ref);
      if (r == 0) delete this;
      return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE Read(void* data, UInt32 size, UInt32* processedSize) override
    {
      if (processedSize) *processedSize = 0;
      if (size == 0) return S_OK;
      DWORD read = 0;
      if (!ReadFile(_file, data, size, &read, NULL))
        return HRESULT_FROM_WIN32(GetLastError());
      if (processedSize) *processedSize = read;
      return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Seek(Int64 offset, UInt32 seekOrigin, UInt64* newPosition) override
    {
      LARGE_INTEGER dist, out;
      dist.QuadPart = offset;
      /* STREAM_SEEK_SET/CUR/END == FILE_BEGIN/CURRENT/END == 0/1/2 */
      if (!SetFilePointerEx(_file, dist, &out, (DWORD)seekOrigin))
        return HRESULT_FROM_WIN32(GetLastError());
      if (newPosition) *newPosition = (UInt64)out.QuadPart;
      return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSize(UInt64* size) override
    {
      LARGE_INTEGER s;
      if (!GetFileSizeEx(_file, &s))
        return HRESULT_FROM_WIN32(GetLastError());
      if (size) *size = (UInt64)s.QuadPart;
      return S_OK;
    }
  };

  /* ---- ISequentialOutStream that appends into a caller-owned vector ------ */
  class CMemOutStream : public ISequentialOutStream
  {
    LONG _ref;
    std::vector<uint8_t>* _buf;
  public:
    explicit CMemOutStream(std::vector<uint8_t>* buf) : _ref(1), _buf(buf) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
      if (!out) return E_POINTER;
      if (iid == IID_IUnknown || iid == IID_ISequentialOutStream)
        *out = static_cast<ISequentialOutStream*>(this);
      else { *out = NULL; return E_NOINTERFACE; }
      AddRef();
      return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
      LONG r = InterlockedDecrement(&_ref);
      if (r == 0) delete this;
      return (ULONG)r;
    }

    HRESULT STDMETHODCALLTYPE Write(const void* data, UInt32 size, UInt32* processedSize) override
    {
      if (size != 0)
      {
        const uint8_t* p = static_cast<const uint8_t*>(data);
        try { _buf->insert(_buf->end(), p, p + size); }
        catch (...) { if (processedSize) *processedSize = 0; return E_OUTOFMEMORY; }
      }
      if (processedSize) *processedSize = size;
      return S_OK;
    }
  };

  /* ---- extract callback: hands each file to the sink once decompressed ---
   * Entry metadata (name/size/crc) comes from the prior enumeration pass, keyed
   * by archive index, so GetStream() doesn't need to query the archive again. */
  class CMemExtractCallback : public IArchiveExtractCallback, public ICryptoGetTextPassword
  {
    LONG _ref;
    const std::unordered_map<uint32_t, sevenzip::EntryInfo>* _info; /* borrowed */
    const sevenzip::EntryDataCallback& _sink;
    std::vector<uint8_t> _data;           /* bytes of the current entry */
    uint32_t _curIndex;                   /* archive index of the current entry */
    bool _hasStream;                      /* current item produced a stream */
    bool _aborted;                        /* sink asked to stop */
  public:
    CMemExtractCallback(const std::unordered_map<uint32_t, sevenzip::EntryInfo>* info,
                        const sevenzip::EntryDataCallback& sink)
      : _ref(1), _info(info), _sink(sink), _curIndex(0), _hasStream(false), _aborted(false) {}

    bool aborted() const { return _aborted; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override
    {
      if (!out) return E_POINTER;
      if (iid == IID_IUnknown)                     *out = static_cast<IUnknown*>(static_cast<IArchiveExtractCallback*>(this));
      else if (iid == IID_IProgress)               *out = static_cast<IProgress*>(this);
      else if (iid == IID_IArchiveExtractCallback) *out = static_cast<IArchiveExtractCallback*>(this);
      else if (iid == IID_ICryptoGetTextPassword)  *out = static_cast<ICryptoGetTextPassword*>(this);
      else { *out = NULL; return E_NOINTERFACE; }
      AddRef();
      return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&_ref); }
    ULONG STDMETHODCALLTYPE Release() override
    {
      LONG r = InterlockedDecrement(&_ref);
      if (r == 0) delete this;
      return (ULONG)r;
    }

    /* IProgress - unused */
    HRESULT STDMETHODCALLTYPE SetTotal(UInt64) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE SetCompleted(const UInt64*) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE GetStream(UInt32 index, ISequentialOutStream** outStream, Int32 askExtractMode) override
    {
      *outStream = NULL;
      _hasStream = false;
      _curIndex = index;
      _data.clear();

      if (askExtractMode != kExtract || _aborted)
        return S_OK;

      /* reserve to avoid repeated reallocation on large entries */
      std::unordered_map<uint32_t, sevenzip::EntryInfo>::const_iterator it = _info->find(index);
      if (it != _info->end() && it->second.size != 0 && it->second.size <= (UInt64)(size_t)-1)
      {
        try { _data.reserve((size_t)it->second.size); } catch (...) {}
      }

      CMemOutStream* stream = new (std::nothrow) CMemOutStream(&_data);
      if (!stream)
        return E_OUTOFMEMORY;
      *outStream = stream;       /* transfer our reference to the caller */
      _hasStream = true;
      return S_OK;
    }

    HRESULT STDMETHODCALLTYPE PrepareOperation(Int32) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE SetOperationResult(Int32 opRes) override
    {
      if (_hasStream && opRes == kOpOK && !_aborted)
      {
        std::unordered_map<uint32_t, sevenzip::EntryInfo>::const_iterator it = _info->find(_curIndex);
        if (it != _info->end())
        {
          if (!_sink(it->second, _data.empty() ? NULL : _data.data(), _data.size()))
            _aborted = true;
        }
      }
      _hasStream = false;
      _data.clear();
      _data.shrink_to_fit();
      return _aborted ? E_ABORT : S_OK;
    }

    /* ICryptoGetTextPassword - we have no password, return empty string */
    HRESULT STDMETHODCALLTYPE CryptoGetTextPassword(BSTR* password) override
    {
      if (password) *password = SysAllocString(L"");
      return S_OK;
    }
  };

  /* ---- 7z.dll loading --------------------------------------------------- */
  Func_CreateObject g_createObject = NULL;

  HMODULE tryLoad(const std::wstring& path)
  {
    if (path.empty()) return NULL;
    return LoadLibraryW(path.c_str());
  }

  std::wstring exeDirDll()
  {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return std::wstring();
    std::wstring p(buf, n);
    size_t slash = p.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return std::wstring();
    return p.substr(0, slash + 1) + L"7z.dll";
  }

  std::wstring registryDll(const wchar_t* valueName)
  {
    wchar_t data[MAX_PATH];
    DWORD size = sizeof(data);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\7-Zip", valueName,
                     RRF_RT_REG_SZ, NULL, data, &size) != ERROR_SUCCESS)
      return std::wstring();
    std::wstring p(data);
    if (!p.empty() && p.back() != L'\\' && p.back() != L'/')
      p += L'\\';
    return p + L"7z.dll";
  }

  bool ensureLoaded(std::string& error)
  {
    if (g_createObject)
      return true;

    HMODULE dll = NULL;

    wchar_t envBuf[MAX_PATH];
    DWORD envLen = GetEnvironmentVariableW(L"RAHASHER_7Z_DLL", envBuf, MAX_PATH);
    if (envLen > 0 && envLen < MAX_PATH)
      dll = tryLoad(std::wstring(envBuf, envLen));

    if (!dll) dll = tryLoad(exeDirDll());
    if (!dll) dll = tryLoad(registryDll(L"Path64"));
    if (!dll) dll = tryLoad(registryDll(L"Path"));
    if (!dll) dll = tryLoad(L"C:\\Program Files\\7-Zip\\7z.dll");
    if (!dll) dll = tryLoad(L"C:\\Program Files (x86)\\7-Zip\\7z.dll");
    if (!dll) dll = tryLoad(L"7z.dll");

    if (!dll)
    {
      error = "could not load 7z.dll - install 7-Zip or set RAHASHER_7Z_DLL to a 7z.dll path";
      return false;
    }

    g_createObject = (Func_CreateObject)GetProcAddress(dll, "CreateObject");
    if (!g_createObject)
    {
      error = "7z.dll does not export CreateObject (incompatible build)";
      return false;
    }
    return true;
  }

  /* candidate format ids to try, best match (by extension) first */
  void candidateFormats(const std::string& path, std::vector<Byte>& out)
  {
    /* extract lowercase extension */
    std::string ext;
    size_t dot = path.find_last_of('.');
    if (dot != std::string::npos)
    {
      ext = path.substr(dot + 1);
      for (size_t i = 0; i < ext.size(); ++i)
        ext[i] = (char)tolower((unsigned char)ext[i]);
    }

    if (ext == "7z")         out.push_back(0x07);
    else if (ext == "zip")   out.push_back(0x01);
    else if (ext == "rar") { out.push_back(0xCC); out.push_back(0x03); }
    else if (ext == "gz" || ext == "gzip") out.push_back(0xEF);
    else if (ext == "bz2")   out.push_back(0x02);
    else if (ext == "xz")    out.push_back(0x0C);
    else if (ext == "tar")   out.push_back(0xEE);

    /* fallback: try the common formats by signature */
    const Byte all[] = {0x07, 0x01, 0xCC, 0x03, 0xEF, 0x02, 0x0C, 0xEE};
    for (size_t i = 0; i < sizeof(all); ++i)
    {
      bool already = false;
      for (size_t j = 0; j < out.size(); ++j) if (out[j] == all[i]) { already = true; break; }
      if (!already) out.push_back(all[i]);
    }
  }
}

namespace sevenzip
{
  bool isArchiveExtension(const std::string& extWithDot)
  {
    return _stricmp(extWithDot.c_str(), ".zip") == 0
        || _stricmp(extWithDot.c_str(), ".7z")  == 0
        || _stricmp(extWithDot.c_str(), ".rar") == 0;
  }

  bool processArchive(const std::string& path, const EntrySelector& select,
                      const EntryDataCallback& cb, std::string& error)
  {
    if (!ensureLoaded(error))
      return false;

    std::vector<Byte> formats;
    candidateFormats(path, formats);

    for (size_t i = 0; i < formats.size(); ++i)
    {
      GUID clsid = formatClsid(formats[i]);
      IInArchive* archive = NULL;
      if (g_createObject(&clsid, &IID_IInArchive, (void**)&archive) != S_OK || !archive)
        continue;

      CFileInStream* fileStream = new (std::nothrow) CFileInStream();
      if (!fileStream) { archive->Release(); error = "out of memory"; return false; }

      if (!fileStream->open(path))
      {
        fileStream->Release();
        archive->Release();
        error = "could not open file: " + path;
        return false;   /* file-level failure - no point trying other formats */
      }

      const UInt64 scanSize = 1 << 23;   /* tolerate SFX / prefixed archives */
      HRESULT hr = archive->Open(fileStream, &scanSize, NULL);
      if (hr != S_OK)
      {
        archive->Close();
        fileStream->Release();
        archive->Release();
        continue;        /* wrong format - try the next candidate */
      }

      /* ---- pass 1: enumerate the file entries (metadata only) ---- */
      UInt32 numItems = 0;
      archive->GetNumberOfItems(&numItems);

      std::vector<EntryInfo> files;
      files.reserve(numItems);
      for (UInt32 idx = 0; idx < numItems; ++idx)
      {
        PROPVARIANT prop;

        memset(&prop, 0, sizeof(prop));
        bool isDir = (archive->GetProperty(idx, kpidIsDir, &prop) == S_OK
                      && prop.vt == VT_BOOL && prop.boolVal != VARIANT_FALSE);
        clearProp(prop);
        if (isDir)
          continue;

        EntryInfo e;
        e.index = idx; e.size = 0; e.crc = 0; e.hasCrc = false;

        memset(&prop, 0, sizeof(prop));
        if (archive->GetProperty(idx, kpidPath, &prop) == S_OK && prop.vt == VT_BSTR && prop.bstrVal)
          e.name = wideToUtf8(prop.bstrVal);
        clearProp(prop);

        memset(&prop, 0, sizeof(prop));
        if (archive->GetProperty(idx, kpidSize, &prop) == S_OK)
        {
          if (prop.vt == VT_UI8)      e.size = prop.uhVal.QuadPart;
          else if (prop.vt == VT_UI4) e.size = prop.ulVal;
        }
        clearProp(prop);

        memset(&prop, 0, sizeof(prop));
        if (archive->GetProperty(idx, kpidCRC, &prop) == S_OK && prop.vt == VT_UI4)
        {
          e.crc = prop.ulVal; e.hasCrc = true;
        }
        clearProp(prop);

        files.push_back(e);
      }

      /* ---- let the caller choose which entries to extract ---- */
      std::vector<uint32_t> indices = select(files);
      std::sort(indices.begin(), indices.end());
      indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

      bool ok = true;
      if (!indices.empty())
      {
        std::unordered_map<uint32_t, EntryInfo> infoMap;
        infoMap.reserve(files.size());
        for (size_t k = 0; k < files.size(); ++k)
          infoMap[files[k].index] = files[k];

        CMemExtractCallback* extractCb = new (std::nothrow) CMemExtractCallback(&infoMap, cb);
        if (!extractCb)
        {
          archive->Close();
          fileStream->Release();
          archive->Release();
          error = "out of memory";
          return false;
        }

        /* ---- pass 2: decompress only the selected entries, into memory ---- */
        hr = archive->Extract(indices.data(), (UInt32)indices.size(), 0, extractCb);
        bool aborted = extractCb->aborted();
        extractCb->Release();

        if (hr != S_OK && !aborted)
        {
          error = "extraction failed for: " + path;
          ok = false;
        }
      }

      archive->Close();
      fileStream->Release();
      archive->Release();
      return ok;
    }

    error = "unrecognized or unsupported archive format: " + path;
    return false;
  }
}

#else /* !_WIN32 */

namespace sevenzip
{
  bool isArchiveExtension(const std::string& extWithDot)
  {
    return strcasecmp(extWithDot.c_str(), ".zip") == 0
        || strcasecmp(extWithDot.c_str(), ".7z")  == 0
        || strcasecmp(extWithDot.c_str(), ".rar") == 0;
  }

  bool processArchive(const std::string&, const EntrySelector&, const EntryDataCallback&, std::string& error)
  {
    error = "archive extraction via 7z.dll is only supported on Windows";
    return false;
  }
}

#endif
