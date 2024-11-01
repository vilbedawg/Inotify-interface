#include "include/FileEvent.hpp"
#include <iomanip>

namespace inotify {

FileEvent::FileEvent(const inotify_event* const event)
  : wd(event->wd), mask(event->mask), cookie(event->cookie), filename(event->name)
{
}

FileEvent::~FileEvent() {}

}  // namespace inotify