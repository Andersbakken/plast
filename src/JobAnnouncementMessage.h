/* This file is part of Plast.

   Plast is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Plast is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Plast.  If not, see <http://www.gnu.org/licenses/>. */

#ifndef JobAnnouncementMessage_h
#define JobAnnouncementMessage_h

#include <rct/Message.h>
#include "Plast.h"

class JobAnnouncementMessage : public Message
{
public:
    enum { MessageId = Plast::JobAnnouncementMessageId };
    JobAnnouncementMessage(const Set<String> &announcement = Set<String>())
        : Message(MessageId), mAnnouncement(announcement)
    {}

    const Set<String> &announcement() const { return mAnnouncement; }

    virtual void encode(Serializer &serializer) const { serializer << mAnnouncement; }
    virtual void decode(Deserializer &deserializer) { deserializer >> mAnnouncement; }
private:
    Set<String> mAnnouncement;
};

#endif
