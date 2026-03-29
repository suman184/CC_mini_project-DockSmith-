#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

#define TEMP_FS "./temp_fs"

typedef struct {
    char name[100];
    char workingDir[100];

    char envKeys[10][100];
    char envValues[10][100];
    int envCount;

    char cmd[200];

} Image;

typedef struct {
    char path[256];
    long size;
    time_t mtime;
} FileInfo;

// 🔥 Layer metadata storage
char layerDigests[100][100];
long layerSizes[100];
char layerCreatedBy[100][200];
int layerCount = 0;

// SNAPSHOT
int take_snapshot(const char *base, FileInfo files[], int *count) {
    DIR *dir = opendir(base);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char fullPath[512];
        sprintf(fullPath, "%s/%s", base, entry->d_name);

        struct stat st;
        stat(fullPath, &st);

        if (S_ISDIR(st.st_mode)) {
            take_snapshot(fullPath, files, count);
        } else {
            strcpy(files[*count].path, fullPath);
            files[*count].size = st.st_size;
            files[*count].mtime = st.st_mtime;
            (*count)++;
        }
    }

    closedir(dir);
    return 0;
}

// DIFF
int file_changed(FileInfo *before, int beforeCount, FileInfo *f) {
    for (int i = 0; i < beforeCount; i++) {
        if (strcmp(before[i].path, f->path) == 0) {
            if (before[i].size == f->size &&
                before[i].mtime == f->mtime)
                return 0;
            else
                return 1;
        }
    }
    return 1;
}

// CREATE LAYER
void create_layer(FileInfo *before, int beforeCount,
                  FileInfo *after, int afterCount,
                  const char *instruction) {

    FILE *list = fopen("filelist.txt", "w");

    int changedCount = 0;

    for (int i = 0; i < afterCount; i++) {
        if (file_changed(before, beforeCount, &after[i])) {
            char *relative = strstr(after[i].path, "temp_fs/");
            if (relative) {
                fprintf(list, "%s\n", relative + strlen("temp_fs/"));
                changedCount++;
            }
        }
    }

    fclose(list);

    // 🔴 Skip empty layers
    if (changedCount == 0) {
        printf("📦 No filesystem changes → skipping layer\n");
        system("rm -f filelist.txt");
        return;
    }

    // deterministic tar (use gtar on macOS, tar on Linux)
    system("tar --sort=name --mtime='UTC 1970-01-01' -cf layer.tar -C temp_fs -T filelist.txt 2>/dev/null || gtar --sort=name --mtime='UTC 1970-01-01' -cf layer.tar -C temp_fs -T filelist.txt");

    // hash (use sha256sum on Linux, shasum on macOS)
    system("sha256sum layer.tar > hash.txt 2>/dev/null || shasum -a 256 layer.tar > hash.txt");

    FILE *h = fopen("hash.txt", "r");
    char hash[100];
    fscanf(h, "%s", hash);
    fclose(h);

    // ensure layer directory
    char mkdirCmd[300];
    sprintf(mkdirCmd, "mkdir -p %s/.docksmith/layers", getenv("HOME"));
    system(mkdirCmd);

    // move tar
    char moveCmd[400];
    sprintf(moveCmd, "mv layer.tar %s/.docksmith/layers/%s.tar", getenv("HOME"), hash);
    system(moveCmd);

    // 🔥 STORE METADATA
    strcpy(layerDigests[layerCount], hash);

    char path[400];
    sprintf(path, "%s/.docksmith/layers/%s.tar", getenv("HOME"), hash);

    struct stat st;
    stat(path, &st);

    layerSizes[layerCount] = st.st_size;
    strcpy(layerCreatedBy[layerCount], instruction);

    layerCount++;

    printf("📦 Layer created: sha256:%s\n", hash);
    printf("📁 Stored at: ~/.docksmith/layers/%s.tar\n", hash);

    system("rm -f filelist.txt hash.txt");
}

// ISOLATED CONTAINER EXECUTION WITH CHROOT
int run_in_container(const char *rootfs, char *cmd) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("❌ fork() failed");
        return -1;
    }
    
    if (pid == 0) {
        // CHILD PROCESS
        // Convert rootfs path to absolute if needed
        char abs_rootfs[512];
        if (rootfs[0] == '/') {
            strcpy(abs_rootfs, rootfs);
        } else {
            getcwd(abs_rootfs, sizeof(abs_rootfs));
            strcat(abs_rootfs, "/");
            strcat(abs_rootfs, rootfs);
        }
        
        // Change to the rootfs first
        if (chdir(abs_rootfs) < 0) {
            perror("❌ chdir() to rootfs failed");
            exit(1);
        }
        
        // Apply chroot to isolate filesystem
        if (chroot(".") < 0) {
            perror("❌ chroot() failed");
            exit(1);
        }
        
        // Change to root directory inside container
        if (chdir("/") < 0) {
            perror("❌ chdir() to / inside container failed");
            exit(1);
        }
        
        // Execute the command inside the isolated container
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        
        // If execl returns, it failed
        perror("❌ execl() failed");
        exit(1);
    } else {
        // PARENT PROCESS - wait for child
        int status;
        waitpid(pid, &status, 0);
        
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "❌ Child process terminated by signal %d\n", WTERMSIG(status));
            return -1;
        }
        
        return -1;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: docksmith <command>\n");
        return 1;
    }

    if (strcmp(argv[1], "build") == 0) {
        printf("Build command triggered\n");

        FILE *fp = fopen("Docksmithfile", "r");
        if (!fp) {
            printf("Docksmithfile not found\n");
            return 1;
        }

        char line[256];
        Image currentImage;
        currentImage.envCount = 0;

        while (fgets(line, sizeof(line), fp)) {
            char command[50];
            char args[200];

            line[strcspn(line, "\n")] = 0;

            int count = sscanf(line, "%s %[^\n]", command, args);
            if (count == 1) args[0] = '\0';

            printf("Command: %s | Args: %s\n", command, args);

            // FROM
            if (strcmp(command, "FROM") == 0) {
                printf("-> Handling FROM\n");

                char imageName[100];
                sscanf(args, "%s", imageName);

                char path[200];
                sprintf(path, "%s/.docksmith/images/%s.json", getenv("HOME"), imageName);

                FILE *img = fopen(path, "r");
                if (!img) {
                    printf("❌ Base image not found: %s\n", imageName);
                    return 1;
                }

                printf("✅ Loaded base image: %s\n", imageName);

                strcpy(currentImage.name, imageName);
                strcpy(currentImage.workingDir, "/");

                fclose(img);

                system("rm -rf temp_fs");
                system("mkdir -p temp_fs");
                printf("📦 Temp filesystem initialized\n");
            }

            // COPY
            else if (strcmp(command, "COPY") == 0) {
                printf("-> Handling COPY\n");

                FileInfo before[1000], after[1000];
                int beforeCount = 0, afterCount = 0;

                take_snapshot(TEMP_FS, before, &beforeCount);

                char src[100], dest[100];
                sscanf(args, "%s %s", src, dest);

                char fullDest[200];
                sprintf(fullDest, "%s%s", TEMP_FS, dest);

                char mkdirCmd[300];
                sprintf(mkdirCmd, "mkdir -p %s", fullDest);
                system(mkdirCmd);

                char copyCmd[400];
                sprintf(copyCmd,
                        "rsync -a --exclude=temp_fs --exclude=docksmith %s/ %s/",
                        src, fullDest);
                system(copyCmd);

                take_snapshot(TEMP_FS, after, &afterCount);

                create_layer(before, beforeCount, after, afterCount, "COPY");
            }

            // RUN
            else if (strcmp(command, "RUN") == 0) {
                printf("-> Handling RUN\n");

                FileInfo before[1000], after[1000];
                int beforeCount = 0, afterCount = 0;

                take_snapshot(TEMP_FS, before, &beforeCount);

                // Execute command in isolated container using chroot
                int exit_code = run_in_container(TEMP_FS, args);
                
                if (exit_code != 0) {
                    printf("❌ RUN command failed with exit code: %d\n", exit_code);
                    fclose(fp);
                    return 1;
                }

                take_snapshot(TEMP_FS, after, &afterCount);

                create_layer(before, beforeCount, after, afterCount, "RUN");
            }

            // WORKDIR
            else if (strcmp(command, "WORKDIR") == 0) {
                printf("-> Handling WORKDIR\n");
                strcpy(currentImage.workingDir, args);
                printf("✅ WorkingDir set to: %s\n", currentImage.workingDir);
            }

            // ENV
            else if (strcmp(command, "ENV") == 0) {
                printf("-> Handling ENV\n");

                char key[100], value[100];
                sscanf(args, "%[^=]=%s", key, value);

                strcpy(currentImage.envKeys[currentImage.envCount], key);
                strcpy(currentImage.envValues[currentImage.envCount], value);

                currentImage.envCount++;

                printf("✅ ENV added: %s=%s\n", key, value);
            }

            // CMD
            else if (strcmp(command, "CMD") == 0) {
                printf("-> Handling CMD\n");

                char temp[200];
                strcpy(temp, args);

                temp[strlen(temp)-1] = '\0';
                memmove(temp, temp+1, strlen(temp));

                for (int i = 0; temp[i]; i++) {
                    if (temp[i] == '"' || temp[i] == ',') temp[i] = ' ';
                }

                strcpy(currentImage.cmd, temp);
                printf("✅ CMD set to: %s\n", currentImage.cmd);
            }

            else {
                printf("❌ Unknown instruction: %s\n", command);
                return 1;
            }
        }

        // 🔥 Print layer metadata (debug / verification)
        printf("\n📦 Layers Built:\n");
        for (int i = 0; i < layerCount; i++) {
            printf("Layer %d:\n", i + 1);
            printf("  Digest: sha256:%s\n", layerDigests[i]);
            printf("  Size: %ld bytes\n", layerSizes[i]);
            printf("  CreatedBy: %s\n", layerCreatedBy[i]);
        }

        fclose(fp);
        return 0;
    }
}