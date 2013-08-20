#ifndef FLAGS_H
#define FLAGS_H

class Flags
{
public:
    Flags(uint flags = 0) {
        d.flags = flags;
    }
    void init(uint flags) {
        d.flags = flags;
    }
    enum Flag {
        None = 0x000000,
        AutoZoomEnabled = 0x000001,
        FirstImage = 0x000002,
        UpdateImagesPending = 0x000004,
        ManuallySetIndex = 0x000008,
        IgnoreFailed = 0x000010,
        DetectFileType = 0x000020,
        NoSmoothScale = 0x000040,
        Closing = 0x000080,
        InNextPrev = 0x000100,
        DisplayFileName = 0x000200,
        DisplayThumbnails = 0x000400,
        HidePointer = 0x000800,
        XKludge = 0x001000
    };

    bool test(Flag flag) const {
        return d.flags & flag;
    }
    void set(Flag flag, bool on = true) {
        if (on) {
            d.flags |= flag;
        } else {
            d.flags &= ~flag;
        }
    }
    void unset(Flag flag) {
        d.flags &= ~flag;
    }
    bool toggle(Flag f) {
        set(f, !(d.flags & f));
        return (d.flags & f);
    }
    uint flags() const {
        return d.flags;
    }
private:
    struct {
        uint flags;
    } d;
};

#endif
