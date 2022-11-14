#include <cerrno>
#include <exception>
#include <iostream>
#include <list>
#include <string>

#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include "utils.hpp"

static mode_t open_mode;
static bool strict_loop;
static bool follow_slink;

class DirEntry {
private:
    std::string name;
    const DirEntry* parent;
    std::list<DirEntry*> entries;
    dev_t dev;
    ino_t ino;
    bool loop{false};
    bool cycle{false};
public:
    DirEntry(const char* name, DirEntry* parent = nullptr)
        : name(name), parent(parent) {}
    int fd() const
    {
        std::list<const DirEntry*> path;
        int fd1, fd2;

        for (const DirEntry* entry = this; entry != nullptr;
            entry = entry->parent
        ) {
            try {
                path.push_front(entry);
            } catch (const std::exception& ex) {
                exit_errx("DirEntry::fd(): push_front(): ", ex.what());
            }
        }
        fd1 = AT_FDCWD;
        for (auto entry : path) {
            fd2 = openat(fd1, entry->name.c_str(),
                open_mode & ~(mode_t)(fd1 == AT_FDCWD ? O_NOFOLLOW : 0));
            if (fd2 < 0)
                exit_err("DirEntry::fd(): openat()");
            if (fd1 != AT_FDCWD && close(fd1) < 0)
                exit_err("DirEntry::fd(): close()");
            fd1 = fd2;
        }
        return fd1;
    }
    void walk()
    {
        struct stat statbuf;
        const struct dirent* ent;
        DIR* dir;
        int fd;
        bool is_dir;

        for (const DirEntry* entry = this->parent; entry != nullptr;
            entry = entry->parent
        ) {
            if (this->dev == entry->dev && this->ino == entry->ino) {
                this->loop = true;
                return;
            }
        }
        fd = this->fd();
        if (this->parent == nullptr) {
            if (fstat(fd, &statbuf) < 0)
                exit_err("DirEntry::walk(): fstat()");
            this->dev = statbuf.st_dev;
            this->ino = statbuf.st_ino;
        }
        dir = fdopendir(fd);
        if (dir == NULL)
            exit_err("DirEntry::walk(): fdopendir()");
        for (;;) {
            errno = 0;
            ent = readdir(dir);
            if (ent == NULL) {
                if (errno != 0)
                    exit_err("DirEntry::walk(): readdir()");
                break;
            }
            if (ent->d_name[0] != '.' || (ent->d_name[1] != '\0' &&
                (ent->d_name[1] != '.' || ent->d_name[2] != '\0'))
            ) {
                try {
                    this->entries.push_back(new DirEntry(ent->d_name, this));
                } catch (const std::exception& ex) {
                    exit_err("DirEntry::walk(): push_back(): ", ex.what());
                }
            }
        }
        if (closedir(dir) < 0)
            exit_err("walk(): closedir()");
        fd = -1;
        for (auto entry : this->entries) {
            if (fd < 0)
                fd = this->fd();
            if (fstatat(fd, entry->name.c_str(), &statbuf,
                follow_slink ? 0 : AT_SYMLINK_NOFOLLOW) < 0
            ) {
                if (errno == ELOOP) {
                    entry->cycle = true;
                    continue;
                }
                exit_err("DirEntry::walk(): fstatat()");
            }
            is_dir = S_ISDIR(statbuf.st_mode);
            if (is_dir && follow_slink && !strict_loop && fstatat(fd,
                entry->name.c_str(), &statbuf, AT_SYMLINK_NOFOLLOW) < 0
            ) {
                exit_err("DirEntry::walk(): fstatat()");
            }
            entry->dev = statbuf.st_dev;
            entry->ino = statbuf.st_ino;
            if (is_dir) {
                if (close(fd) < 0)
                    exit_err("DirEntry::walk(): close()");
                fd = -1;
                entry->walk();
            }
        }
        if (fd >= 0 && close(fd) < 0)
            exit_err("DirEntry::walk(): close()");
    }
    void show(unsigned int level = 0) const
    {
        for (const auto entry : this->entries) {
            std::cout.width(level * 2);
            std::cout << "";
            std::cout << entry->name <<
                (entry->loop ? " (loop)" : "") <<
                (entry->cycle ? " (cycle)" : "") << '\n';
            entry->show(level + 1);
        }
    }
};

int
main(int argc, char *argv[])
{
    int opt;

    opterr = 0;
    strict_loop = true;
    follow_slink = false;
    while ((opt = getopt(argc, argv, "LP")) != -1)
        switch (opt) {
        case 'L':
            follow_slink = true;
            break;
        case 'P':
            strict_loop = false;
            break;
        default:
            exit_errx("wrong option -", static_cast<char>(optopt));
        }
    if (optind != argc - 1)
        exit_errx("wrong arguments number");
    open_mode = O_RDONLY|O_DIRECTORY;
    if (!follow_slink)
        open_mode |= O_NOFOLLOW;
    if (!follow_slink && !strict_loop)
        exit_errx("the -P flag should be used with -L");
    DirEntry* entry;
    try {
        entry = new DirEntry(argv[optind]);
    } catch (const std::exception& ex) {
        exit_errx("new: ", ex.what());
    }
    entry->walk();
    entry->show();
}
