/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2012 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "FileManager.hpp"
#include "WidgetDialog.hpp"
#include "UIGlobals.hpp"
#include "Look/DialogLook.hpp"
#include "Form/List.hpp"
#include "Form/ListWidget.hpp"
#include "Screen/Layout.hpp"
#include "Language/Language.hpp"
#include "LocalPath.hpp"
#include "OS/FileUtil.hpp"
#include "OS/PathName.hpp"
#include "IO/FileLineReader.hpp"
#include "Formatter/ByteSizeFormatter.hpp"
#include "Formatter/TimeFormatter.hpp"
#include "DateTime.hpp"
#include "Net/Features.hpp"
#include "Util/ConvertString.hpp"
#include "Repository/FileRepository.hpp"
#include "Repository/Parser.hpp"

#ifdef HAVE_DOWNLOAD_MANAGER
#include "ListPicker.hpp"
#include "Form/Button.hpp"
#include "Net/DownloadManager.hpp"
#include "Thread/Notify.hpp"
#include "Thread/Mutex.hpp"
#include "Timer.hpp"

#include <map>
#include <vector>
#endif

#include <assert.h>
#include <windef.h> /* for MAX_PATH */

#define REPOSITORY_URI "http://download.xcsoar.org/repository"

static bool
LocalPath(TCHAR *buffer, const AvailableFile &file)
{
  ACPToWideConverter base(file.GetName());
  if (!base.IsValid())
    return false;

  ::LocalPath(buffer, base);
  return true;
}

#ifdef HAVE_DOWNLOAD_MANAGER

gcc_pure
static const AvailableFile *
FindRemoteFile(const FileRepository &repository, const char *name)
{
  return repository.FindByName(name);
}

#ifdef _UNICODE
gcc_pure
static const AvailableFile *
FindRemoteFile(const FileRepository &repository, const TCHAR *name)
{
  WideToACPConverter name2(name);
  if (!name2.IsValid())
    return NULL;

  return FindRemoteFile(repository, name2);
}
#endif

#endif

class ManagedFileListWidget
  : public ListWidget,
#ifdef HAVE_DOWNLOAD_MANAGER
    private Timer, private Net::DownloadListener, private Notify,
#endif
    private ActionListener {
  enum Buttons {
    DOWNLOAD,
    ADD,
  };

  struct DownloadStatus {
    int64_t size, position;
  };

  struct FileItem {
    StaticString<64u> name;
    StaticString<32u> size;
    StaticString<32u> last_modified;

    bool downloading;

    DownloadStatus download_status;

    void Set(const TCHAR *_name, const DownloadStatus *_download_status) {
      name = _name;

      TCHAR path[MAX_PATH];
      LocalPath(path, name);

      if (File::Exists(path)) {
        FormatByteSize(size.buffer(), size.MAX_SIZE,
                       File::GetSize(path));
#ifdef HAVE_POSIX
        FormatISO8601(last_modified.buffer(),
                      BrokenDateTime::FromUnixTimeUTC(File::GetLastModification(path)));
#else
        // XXX implement
        last_modified.clear();
#endif
      } else {
        size.clear();
        last_modified.clear();
      }

      downloading = _download_status != NULL;
      if (downloading)
        download_status = *_download_status;
    }
  };

  UPixelScalar font_height;

#ifdef HAVE_DOWNLOAD_MANAGER
  WndButton *download_button, *add_button;
#endif

  FileRepository repository;

#ifdef HAVE_DOWNLOAD_MANAGER
  /**
   * This mutex protects the attributes "downloads" and
   * "repository_modified".
   */
  mutable Mutex mutex;

  /**
   * The list of file names (base names) that are currently being
   * downloaded.
   */
  std::map<std::string, DownloadStatus> downloads;

  /**
   * Was the repository file modified, and needs to be reloaded by
   * LoadRepositoryFile()?
   */
  bool repository_modified;
#endif

  TrivialArray<FileItem, 64u> items;

public:
  void CreateButtons(WidgetDialog &dialog);

protected:
  gcc_pure
  bool IsDownloading(const char *name) const {
#ifdef HAVE_DOWNLOAD_MANAGER
    ScopeLock protect(mutex);
    return downloads.find(name) != downloads.end();
#else
    return false;
#endif
  }

  gcc_pure
  bool IsDownloading(const AvailableFile &file) const {
    return IsDownloading(file.GetName());
  }

  gcc_pure
  bool IsDownloading(const char *name, DownloadStatus &status_r) const {
#ifdef HAVE_DOWNLOAD_MANAGER
    ScopeLock protect(mutex);
    auto i = downloads.find(name);
    if (i == downloads.end())
      return false;

    status_r = i->second;
    return true;
#else
    return false;
#endif
  }

  gcc_pure
  bool IsDownloading(const AvailableFile &file,
                     DownloadStatus &status_r) const {
    return IsDownloading(file.GetName(), status_r);
  }

  gcc_pure
  int FindItem(const TCHAR *name) const;

  void LoadRepositoryFile();
  void RefreshList();
  void UpdateButtons();

  void Download();
  void Add();

public:
  /* virtual methods from class Widget */
  virtual void Prepare(ContainerWindow &parent, const PixelRect &rc);
  virtual void Unprepare();

  /* virtual methods from class List::Handler */
  virtual void OnPaintItem(Canvas &canvas, const PixelRect rc,
                           unsigned idx);
  virtual void OnCursorMoved(unsigned index);

  /* virtual methods from class ActionListener */
  virtual void OnAction(int id);

#ifdef HAVE_DOWNLOAD_MANAGER
  /* virtual methods from class Timer */
  virtual void OnTimer() gcc_override;

  /* virtual methods from class Net::DownloadListener */
  virtual void OnDownloadAdded(const TCHAR *path_relative,
                               int64_t size, int64_t position) gcc_override;
  virtual void OnDownloadComplete(const TCHAR *path_relative, bool success);

  /* virtual methods from class Notify */
  virtual void OnNotification();
#endif
};

void
ManagedFileListWidget::Prepare(ContainerWindow &parent, const PixelRect &rc)
{
  const DialogLook &look = UIGlobals::GetDialogLook();
  UPixelScalar margin = Layout::Scale(2);
  font_height = look.list.font->GetHeight();

  UPixelScalar row_height = std::max(UPixelScalar(3 * margin + 2 * font_height),
                                     Layout::GetMaximumControlHeight());
  CreateList(parent, look, rc, row_height);
  LoadRepositoryFile();
  RefreshList();
  UpdateButtons();

#ifdef HAVE_DOWNLOAD_MANAGER
  if (Net::DownloadManager::IsAvailable()) {
    Net::DownloadManager::AddListener(*this);
    Net::DownloadManager::Enumerate(*this);

    Net::DownloadManager::Enqueue(REPOSITORY_URI, _T("repository"));
  }
#endif
}

void
ManagedFileListWidget::Unprepare()
{
#ifdef HAVE_DOWNLOAD_MANAGER
  Timer::Cancel();

  if (Net::DownloadManager::IsAvailable())
    Net::DownloadManager::RemoveListener(*this);

  ClearNotification();
#endif

  DeleteWindow();
}

int
ManagedFileListWidget::FindItem(const TCHAR *name) const
{
  for (auto i = items.begin(), end = items.end(); i != end; ++i)
    if (StringIsEqual(i->name, name))
      return std::distance(items.begin(), i);

  return -1;
}

void
ManagedFileListWidget::LoadRepositoryFile()
{
#ifdef HAVE_DOWNLOAD_MANAGER
  mutex.Lock();
  repository_modified = false;
  mutex.Unlock();
#endif

  repository.Clear();

  TCHAR path[MAX_PATH];
  LocalPath(path, _T("repository"));
  FileLineReaderA reader(path);
  if (reader.error())
    return;

  ParseFileRepository(repository, reader);
}

void
ManagedFileListWidget::RefreshList()
{
  items.clear();

  bool download_active = false;
  for (auto i = repository.begin(), end = repository.end(); i != end; ++i) {
    const auto &remote_file = *i;
    DownloadStatus download_status;
    const bool is_downloading = IsDownloading(remote_file, download_status);

    TCHAR path[MAX_PATH];
    if (LocalPath(path, remote_file) &&
        (is_downloading || File::Exists(path))) {
      download_active |= is_downloading;
      items.append().Set(BaseName(path),
                         is_downloading ? &download_status : NULL);
    }
  }

  ListControl &list = GetList();
  list.SetLength(items.size());
  list.Invalidate();

#ifdef HAVE_DOWNLOAD_MANAGER
  if (download_active && !Timer::IsActive())
    Timer::Schedule(1000);
#endif
}

void
ManagedFileListWidget::CreateButtons(WidgetDialog &dialog)
{
#ifdef HAVE_DOWNLOAD_MANAGER
  if (Net::DownloadManager::IsAvailable()) {
    download_button = dialog.AddButton(_("Download"), this, DOWNLOAD);
    add_button = dialog.AddButton(_("Add"), this, ADD);
  }
#endif
}

void
ManagedFileListWidget::UpdateButtons()
{
#ifdef HAVE_DOWNLOAD_MANAGER
  if (Net::DownloadManager::IsAvailable()) {
    const unsigned current = GetList().GetCursorIndex();

    download_button->SetEnabled(!items.empty() &&
                                FindRemoteFile(repository,
                                               items[current].name) != NULL);
  }
#endif
}

void
ManagedFileListWidget::OnPaintItem(Canvas &canvas, const PixelRect rc,
                                   unsigned i)
{
  const FileItem &file = items[i];

  const UPixelScalar margin = Layout::Scale(2);

  canvas.text(rc.left + margin, rc.top + margin, file.name.c_str());

  if (file.downloading) {
    StaticString<64> text;
    if (file.download_status.position < 0) {
      text = _("Queued");
    } else if (file.download_status.size > 0) {
      text.Format(_T("%s (%u%%)"), _("Downloading"),
                    unsigned(file.download_status.position * 100
                             / file.download_status.size));
    } else {
      TCHAR size[32];
      FormatByteSize(size, ARRAY_SIZE(size), file.download_status.position);
      text.Format(_T("%s (%s)"), _("Downloading"), size);
    }

    UPixelScalar width = canvas.CalcTextWidth(text);
    canvas.text(rc.right - width - margin, rc.top + margin, text);
  }

  canvas.text(rc.left + margin, rc.top + 2 * margin + font_height,
              file.size.c_str());

  canvas.text((rc.left + rc.right) / 2, rc.top + 2 * margin + font_height,
              file.last_modified.c_str());
}

void
ManagedFileListWidget::OnCursorMoved(unsigned index)
{
  UpdateButtons();
}

void
ManagedFileListWidget::Download()
{
#ifdef HAVE_DOWNLOAD_MANAGER
  assert(Net::DownloadManager::IsAvailable());

  if (items.empty())
    return;

  const unsigned current = GetList().GetCursorIndex();
  assert(current < items.size());

  const FileItem &item = items[current];
  const AvailableFile *remote_file_p = FindRemoteFile(repository, item.name);
  if (remote_file_p == NULL)
    return;

  const AvailableFile &remote_file = *remote_file_p;
  ACPToWideConverter base(remote_file.GetName());
  if (!base.IsValid())
    return;

  Net::DownloadManager::Enqueue(remote_file.uri.c_str(), base);
#endif
}

#ifdef HAVE_DOWNLOAD_MANAGER

static const std::vector<AvailableFile> *add_list;

static void
OnPaintAddItem(Canvas &canvas, const PixelRect rc, unsigned i)
{
  assert(add_list != NULL);
  assert(i < add_list->size());

  const AvailableFile &file = (*add_list)[i];

  ACPToWideConverter name(file.GetName());
  if (name.IsValid())
    canvas.text(rc.left + Layout::Scale(2), rc.top + Layout::Scale(2), name);
}

#endif

void
ManagedFileListWidget::Add()
{
#ifdef HAVE_DOWNLOAD_MANAGER
  assert(Net::DownloadManager::IsAvailable());

  if (items.empty())
    return;

  std::vector<AvailableFile> list;
  for (auto i = repository.begin(), end = repository.end(); i != end; ++i) {
    const AvailableFile &remote_file = *i;

    if (IsDownloading(remote_file.GetName()))
      /* already downloading this file */
      continue;

    ACPToWideConverter name(remote_file.GetName());
    if (!name.IsValid())
      continue;

    if (FindItem(name) < 0)
      list.push_back(remote_file);
  }

  if (list.empty())
    return;

  add_list = &list;
  int i = ListPicker(UIGlobals::GetMainWindow(), _("Select a file"),
                     list.size(), 0, Layout::FastScale(18),
                     OnPaintAddItem);
  add_list = NULL;
  if (i < 0)
    return;

  assert((unsigned)i < list.size());

  const AvailableFile &remote_file = list[i];
  ACPToWideConverter base(remote_file.GetName());
  if (!base.IsValid())
    return;

  Net::DownloadManager::Enqueue(remote_file.GetURI(), base);
#endif
}

void
ManagedFileListWidget::OnAction(int id)
{
  switch (id) {
  case DOWNLOAD:
    Download();
    break;

  case ADD:
    Add();
    break;
  }
}

#ifdef HAVE_DOWNLOAD_MANAGER

void
ManagedFileListWidget::OnTimer()
{
  mutex.Lock();
  const bool download_active = !downloads.empty();
  mutex.Unlock();

  if (download_active) {
    Net::DownloadManager::Enumerate(*this);
    RefreshList();
    Timer::Schedule(1000);
  }
}

void
ManagedFileListWidget::OnDownloadAdded(const TCHAR *path_relative,
                                       int64_t size, int64_t position)
{
  const TCHAR *name = BaseName(path_relative);
  if (name == NULL)
    return;

  WideToACPConverter name2(name);
  if (!name2.IsValid())
    return;

  mutex.Lock();
  downloads[(const char *)name2] = DownloadStatus{size, position};
  mutex.Unlock();

  SendNotification();
}

void
ManagedFileListWidget::OnDownloadComplete(const TCHAR *path_relative,
                                          bool success)
{
  const TCHAR *name = BaseName(path_relative);
  if (name == NULL)
    return;

  WideToACPConverter name2(name);
  if (!name2.IsValid())
    return;

  mutex.Lock();

  downloads.erase((const char *)name2);

  if (StringIsEqual(name2, "repository"))
    repository_modified = true;

  mutex.Unlock();

  SendNotification();
}

void
ManagedFileListWidget::OnNotification()
{
  mutex.Lock();
  bool repository_modified2 = repository_modified;
  mutex.Unlock();

  if (repository_modified2)
    LoadRepositoryFile();

  RefreshList();
}

#endif

void
ShowFileManager()
{
  ManagedFileListWidget widget;
  WidgetDialog dialog(_("File Manager"), &widget);
  dialog.AddButton(_("Close"), mrOK);
  widget.CreateButtons(dialog);

  dialog.ShowModal();
  dialog.StealWidget();
}
