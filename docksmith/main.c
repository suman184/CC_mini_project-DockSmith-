#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <ctype.h>

#define TEMP_FS "./temp_fs"

typedef struct {
    char name[100];
    char tag[100];
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

// Image manifest for run command
typedef struct {
    char layers[100][100];
    int layerCount;
    char cmd[200];
    char env[10][200];
    int envCount;
    char workingDir[100];
} ImageManifest;

// SNAPSHOT - Skip system directories to avoid huge file lists
int take_snapshot(const char *base, FileInfo files[], int *count) {
    DIR *dir = opendir(base);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        
        // Skip system directories to keep snapshot size manageable
        if (strcmp(entry->d_name, "bin") == 0 || strcmp(entry->d_name, "lib") == 0 ||
            strcmp(entry->d_name, "lib64") == 0 || strcmp(entry->d_name, "usr") == 0) {
            continue;
        }

        // Safety check to prevent array overflow (arrays sized 1000)
        if (*count >= 999) {
            fprintf(stderr, "⚠️  Warning: snapshot array full (user files: %d)\n", *count);
            closedir(dir);
            return 0;  // Return successfully rather than failing
        }

        char fullPath[512];
        sprintf(fullPath, "%s/%s", base, entry->d_name);

        struct stat st;
        if (stat(fullPath, &st) < 0) continue;

        if (S_ISDIR(st.st_mode)) {
            take_snapshot(fullPath, files, count);
        } else {
            strncpy(files[*count].path, fullPath, 255);
            files[*count].path[255] = '\0';
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

// ISOLATED CONTAINER EXECUTION WITH CHROOT
int run_in_container(const char *rootfs, char *cmd) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("❌ fork() failed");
        return -1;
    }
    
    if (pid == 0) {
        // CHILD PROCESS
        char abs_rootfs[512];
        if (rootfs[0] == '/') {
            strcpy(abs_rootfs, rootfs);
        } else {
            getcwd(abs_rootfs, sizeof(abs_rootfs));
            strcat(abs_rootfs, "/");
            strcat(abs_rootfs, rootfs);
        }
        
        if (chdir(abs_rootfs) < 0) {
            perror("❌ chdir() to rootfs failed");
            exit(1);
        }
        
        if (chroot(".") < 0) {
            perror("❌ chroot() failed");
            exit(1);
        }
        
        if (chdir("/") < 0) {
            perror("❌ chdir() to / inside container failed");
            exit(1);
        }
        
        setenv("LD_LIBRARY_PATH", "/lib:/lib64", 1);
        
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        
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

    // Always create a layer (even if empty)
    // deterministic tar (use gtar on macOS, tar on Linux)
    if (changedCount == 0) {
        // Create empty tar file
        system("tar --sort=name --mtime='UTC 1970-01-01' -cf layer.tar --files-from /dev/null 2>/dev/null || gtar --sort=name --mtime='UTC 1970-01-01' -cf layer.tar --files-from /dev/null");
    } else {
        // Create tar with changed files
        system("tar --sort=name --mtime='UTC 1970-01-01' -cf layer.tar -C temp_fs -T filelist.txt 2>/dev/null || gtar --sort=name --mtime='UTC 1970-01-01' -cf layer.tar -C temp_fs -T filelist.txt");
    }

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

    // check if layer already exists (cache hit/miss detection)
    char layerPath[400];
    sprintf(layerPath, "%s/.docksmith/layers/%s.tar", getenv("HOME"), hash);
    
    struct stat st;
    int cacheHit = (stat(layerPath, &st) == 0);

    if (cacheHit) {
        printf("  [CACHE HIT]\n");
    } else {
        // move tar
        char moveCmd[400];
        sprintf(moveCmd, "mv layer.tar %s/.docksmith/layers/%s.tar", getenv("HOME"), hash);
        system(moveCmd);
        
        stat(layerPath, &st);
        printf("  [CACHE MISS]\n");
    }

    // 🔥 STORE METADATA
    strcpy(layerDigests[layerCount], hash);
    layerSizes[layerCount] = st.st_size;
    strcpy(layerCreatedBy[layerCount], instruction);
    layerCount++;

    printf("📦 Layer: sha256:%s\n", hash);

    system("rm -f filelist.txt hash.txt layer.tar");
}

// Container execution with working directory support
int run_container_with_workdir(const char *rootfs, const char *cmd, const char *workingDir) {
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("❌ fork() failed");
        return -1;
    }
    
    if (pid == 0) {
        // CHILD PROCESS
        char abs_rootfs[512];
        if (rootfs[0] == '/') {
            strcpy(abs_rootfs, rootfs);
        } else {
            getcwd(abs_rootfs, sizeof(abs_rootfs));
            strcat(abs_rootfs, "/");
            strcat(abs_rootfs, rootfs);
        }
        
        if (chdir(abs_rootfs) < 0) {
            perror("❌ chdir() to rootfs failed");
            exit(1);
        }
        
        if (chroot(".") < 0) {
            perror("❌ chroot() failed");
            exit(1);
        }
        
        // Change to working directory inside container
        if (workingDir && workingDir[0] != '\0') {
            if (chdir(workingDir) < 0) {
                perror("❌ chdir() to working directory failed");
                exit(1);
            }
        } else {
            if (chdir("/") < 0) {
                perror("❌ chdir() to / failed");
                exit(1);
            }
        }
        
        setenv("LD_LIBRARY_PATH", "/lib:/lib64", 1);
        
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        
        perror("❌ execl() failed");
        exit(1);
    } else {
        // PARENT PROCESS
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

// Load image manifest for run command
int load_manifest(const char *imageName, ImageManifest *manifest) {
    char path[512];
    sprintf(path, "%s/.docksmith/images/%s.json", getenv("HOME"), imageName);
    
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("❌ Image not found: %s\n", imageName);
        return -1;
    }
    
    // Initialize manifest
    manifest->layerCount = 0;
    manifest->envCount = 0;
    strcpy(manifest->workingDir, "/");
    strcpy(manifest->cmd, "");
    
    // Simple JSON parsing - scan for key-value pairs
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        // Parse layers
        if (strstr(line, "\"layers\"")) {
            while (fgets(line, sizeof(line), fp)) {
                if (strchr(line, ']')) break;  // End of layers array
                if (strstr(line, "\"")) {
                    char *start = strchr(line, '"');
                    if (start) {
                        start++;
                        char *end = strchr(start, '"');
                        if (end && start != end) {
                            int len = end - start;
                            // Only accept valid hex strings (SHA256 = 64 chars)
                            if (len == 64) {
                                int valid = 1;
                                for (int j = 0; j < len; j++) {
                                    if (!isxdigit(start[j])) {
                                        valid = 0;
                                        break;
                                    }
                                }
                                if (valid && manifest->layerCount < 100) {
                                    strncpy(manifest->layers[manifest->layerCount], start, len);
                                    manifest->layers[manifest->layerCount][len] = '\0';
                                    manifest->layerCount++;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        // Parse Cmd
        if (strstr(line, "\"Cmd\"")) {
            char *start = strchr(line, ':');
            if (start) {
                start = strchr(start, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        strncpy(manifest->cmd, start, end - start);
                        manifest->cmd[end - start] = '\0';
                    }
                }
            }
        }
        
        // Parse WorkingDir
        if (strstr(line, "\"WorkingDir\"")) {
            char *start = strchr(line, ':');
            if (start) {
                start = strchr(start, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        strncpy(manifest->workingDir, start, end - start);
                        manifest->workingDir[end - start] = '\0';
                    }
                }
            }
        }
        
        // Parse Env array
        if (strstr(line, "\"Env\"")) {
            while (fgets(line, sizeof(line), fp)) {
                if (strchr(line, ']')) break;
                if (strstr(line, "\"")) {
                    char *start = strchr(line, '"');
                    if (start) {
                        start++;
                        char *end = strchr(start, '"');
                        if (end && manifest->envCount < 10) {
                            strncpy(manifest->env[manifest->envCount], start, end - start);
                            manifest->env[manifest->envCount][end - start] = '\0';
                            manifest->envCount++;
                        }
                    }
                }
            }
        }
    }
    
    fclose(fp);
    printf("✅ Manifest loaded: %d layers, %d env vars\n", manifest->layerCount, manifest->envCount);
    return 0;
}

// Extract layers into runtime filesystem
int extract_layers(ImageManifest *manifest) {
    printf("📦 Extracting %d layers...\n", manifest->layerCount);
    
    for (int i = 0; i < manifest->layerCount; i++) {
        char tarPath[512];
        sprintf(tarPath, "%s/.docksmith/layers/%s.tar", getenv("HOME"), manifest->layers[i]);
        
        char cmd[1024];
        sprintf(cmd, "tar -xf %s -C runtime_fs/ 2>&1", tarPath);
        
        printf("  Layer %d: %s\n", i + 1, manifest->layers[i]);
        int ret = system(cmd);
        if (ret != 0) {
            printf("❌ Failed to extract layer: %s\n", manifest->layers[i]);
            return -1;
        }
    }
    
    printf("✅ All layers extracted\n");
    
    // Ensure runtime_fs has shell and libraries for execution
    system("mkdir -p runtime_fs/bin runtime_fs/lib runtime_fs/lib64 2>&1");
    system("cp -L /bin/sh runtime_fs/bin/ 2>&1");
    system("cp -L /lib/aarch64-linux-gnu/*.so* runtime_fs/lib/ 2>/dev/null || true");
    system("cp -L /lib64/*.so* runtime_fs/lib64/ 2>/dev/null || true");
    
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: docksmith build -t <name:tag> <context>\n");
        printf("       docksmith run <name:tag> [cmd] [-e KEY=VALUE]\n");
        printf("       docksmith images\n");
        printf("       docksmith rmi <name:tag>\n");
        return 1;
    }

    if (strcmp(argv[1], "build") == 0) {
        if (argc < 5 || strcmp(argv[2], "-t") != 0) {
            printf("Usage: docksmith build -t <name:tag> <context>\n");
            return 1;
        }
        
        char *imageRef = argv[3];
        char *context = argv[4];
        char imageName[100], imageTag[100];
        
        char *colon = strchr(imageRef, ':');
        if (colon) {
            strncpy(imageName, imageRef, colon - imageRef);
            imageName[colon - imageRef] = '\0';
            strcpy(imageTag, colon + 1);
        } else {
            strcpy(imageName, imageRef);
            strcpy(imageTag, "latest");
        }
        
        printf("Building: %s:%s from %s\n\n", imageName, imageTag, context);

        char docksmithPath[512];
        sprintf(docksmithPath, "%s/Docksmithfile", context);
        FILE *fp = fopen(docksmithPath, "r");
        if (!fp) {
            printf("Docksmithfile not found: %s\n", docksmithPath);
            return 1;
        }

        char line[256];
        Image currentImage;
        currentImage.envCount = 0;
        strcpy(currentImage.name, imageName);
        strcpy(currentImage.tag, imageTag);

        // Count total steps
        int totalSteps = 0;
        FILE *count_fp = fopen(docksmithPath, "r");
        char count_line[256];
        while (fgets(count_line, sizeof(count_line), count_fp)) {
            count_line[strcspn(count_line, "\n")] = 0;
            if (strlen(count_line) > 0 && count_line[0] != '#') totalSteps++;
        }
        fclose(count_fp);
        
        int currentStep = 0;

        while (fgets(line, sizeof(line), fp)) {
            char command[50];
            char args[200];

            line[strcspn(line, "\n")] = 0;

            int count = sscanf(line, "%s %[^\n]", command, args);
            if (count == 1) args[0] = '\0';
            
            if (strlen(line) == 0 || line[0] == '#') continue;
            
            currentStep++;
            printf("Step %d/%d : %s %s\n", currentStep, totalSteps, command, args);

            // FROM
            if (strcmp(command, "FROM") == 0) {
                printf("-> Handling FROM\n");
                fflush(stdout);

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
                fflush(stdout);

                strcpy(currentImage.workingDir, "/");

                fclose(img);

                printf("   Cleaning up old temp_fs\n");
                fflush(stdout);
                system("rm -rf temp_fs 2>&1");
                
                printf("   Creating directories\n");
                fflush(stdout);
                system("mkdir -p temp_fs/bin temp_fs/lib temp_fs/lib64 2>&1");
                
                printf("   Copying sh binary\n");
                fflush(stdout);
                system("cp -L /bin/sh temp_fs/bin/ 2>&1");
                
                printf("   Copying libraries (aarch64)\n");
                fflush(stdout);
                // Copy all .so* files from aarch64 lib directory (follows symlinks with -L)
                system("cp -L /lib/aarch64-linux-gnu/*.so* temp_fs/lib/ 2>/dev/null || true");
                system("cp -L /lib/aarch64-linux-gnu/ld-linux-aarch64.so.1 temp_fs/lib/ 2>/dev/null || true");
                // Fallback to /lib64 if needed
                system("mkdir -p temp_fs/lib64 && cp -L /lib64/*.so* temp_fs/lib64/ 2>/dev/null || true");
                
                printf("📦 Temp filesystem initialized\n");
                fflush(stdout);
            }

            // COPY
            else if (strcmp(command, "COPY") == 0) {
                printf("-> Handling COPY\n");
                fflush(stdout);

                // Use smaller arrays to avoid stack overflow
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
                fflush(stdout);

                // Use smaller arrays
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

        // Save image manifest
        printf("Saving manifest...\n");
        char manifestPath[512];
        sprintf(manifestPath, "%s/.docksmith/images/%s_%s.json", getenv("HOME"), currentImage.name, currentImage.tag);
        FILE *manifest_fp = fopen(manifestPath, "w");
        if (manifest_fp) {
            fprintf(manifest_fp, "{\n");
            fprintf(manifest_fp, "  \"name\": \"%s\",\n", currentImage.name);
            fprintf(manifest_fp, "  \"tag\": \"%s\",\n", currentImage.tag);
            fprintf(manifest_fp, "  \"layers\": [\n");
            for (int i = 0; i < layerCount; i++) {
                fprintf(manifest_fp, "    \"%s\"", layerDigests[i]);
                if (i < layerCount - 1) fprintf(manifest_fp, ",");
                fprintf(manifest_fp, "\n");
            }
            fprintf(manifest_fp, "  ],\n");
            fprintf(manifest_fp, "  \"config\": {\n");
            fprintf(manifest_fp, "    \"Cmd\": \"%s\",\n", currentImage.cmd);
            fprintf(manifest_fp, "    \"WorkingDir\": \"%s\",\n", currentImage.workingDir);
            fprintf(manifest_fp, "    \"Env\": [\n");
            for (int i = 0; i < currentImage.envCount; i++) {
                fprintf(manifest_fp, "      \"%s=%s\"", currentImage.envKeys[i], currentImage.envValues[i]);
                if (i < currentImage.envCount - 1) fprintf(manifest_fp, ",");
                fprintf(manifest_fp, "\n");
            }
            fprintf(manifest_fp, "    ]\n");
            fprintf(manifest_fp, "  }\n");
            fprintf(manifest_fp, "}\n");
            fclose(manifest_fp);
            printf("Manifest saved\n");
        }

        fclose(fp);
        printf("\nSuccessfully built %s:%s\n", currentImage.name, currentImage.tag);
        return 0;
    }

    // RUN command
    else if (strcmp(argv[1], "run") == 0) {
        if (argc < 3) {
            printf("Usage: docksmith run <name:tag> [cmd] [-e KEY=VALUE]\n");
            return 1;
        }
        
        char imageName[100], imageTag[100];
        char *imageRef = argv[2];
        char *colon = strchr(imageRef, ':');
        if (colon) {
            strncpy(imageName, imageRef, colon - imageRef);
            imageName[colon - imageRef] = '\0';
            strcpy(imageTag, colon + 1);
        } else {
            strcpy(imageName, imageRef);
            strcpy(imageTag, "latest");
        }
        
        char manifestName[200];
        sprintf(manifestName, "%s_%s", imageName, imageTag);
        
        const char *override_cmd = NULL;
        int env_count = 0;
        char env_overrides[10][200];
        
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
                strcpy(env_overrides[env_count++], argv[i + 1]);
                i++;
            } else if (override_cmd == NULL) {
                override_cmd = argv[i];
            }
        }
        
        printf("🚀 Running container: %s:%s\n", imageName, imageTag);
        fflush(stdout);
        
        // Load image manifest
        ImageManifest manifest;
        if (load_manifest(manifestName, &manifest) < 0) {
            return 1;
        }
        
        printf("✅ Manifest loaded (%d layers)\n", manifest.layerCount);
        fflush(stdout);
        
        // Create runtime filesystem
        printf("📂 Creating runtime filesystem...\n");
        fflush(stdout);
        system("rm -rf runtime_fs 2>&1");
        system("mkdir -p runtime_fs 2>&1");
        
        // Extract layers
        if (extract_layers(&manifest) < 0) {
            return 1;
        }
        
        // Apply environment variables
        printf("🔧 Setting environment variables...\n");
        for (int i = 0; i < manifest.envCount; i++) {
            char envCopy[200];
            strcpy(envCopy, manifest.env[i]);
            char *eq = strchr(envCopy, '=');
            if (eq) {
                *eq = '\0';
                setenv(envCopy, eq + 1, 1);
                printf("  %s=%s\n", envCopy, eq + 1);
            }
        }
        
        // Apply ENV overrides
        for (int i = 0; i < env_count; i++) {
            char envCopy[200];
            strcpy(envCopy, env_overrides[i]);
            char *eq = strchr(envCopy, '=');
            if (eq) {
                *eq = '\0';
                setenv(envCopy, eq + 1, 1);
                printf("  %s=%s (override)\n", envCopy, eq + 1);
            }
        }
        
        // Determine command to run
        char finalCmd[512];
        if (override_cmd) {
            strcpy(finalCmd, override_cmd);
            printf("📝 Override command: %s\n", finalCmd);
        } else if (manifest.cmd[0] != '\0') {
            strcpy(finalCmd, manifest.cmd);
            printf("📝 Default command: %s\n", finalCmd);
        } else {
            printf("❌ No command specified (no CMD in manifest and no override)\n");
            return 1;
        }
        
        printf("📍 Working dir: %s\n", manifest.workingDir);
        fflush(stdout);
        
        // Execute in container
        printf("🏃 Executing...\n\n");
        fflush(stdout);
        
        int exit_code = run_container_with_workdir("runtime_fs", finalCmd, manifest.workingDir);
        
        printf("\n✅ Container exited with code: %d\n", exit_code);
        
        return exit_code;
    }

    else if (strcmp(argv[1], "images") == 0) {
        char imagesDir[512];
        sprintf(imagesDir, "%s/.docksmith/images", getenv("HOME"));
        DIR *dir = opendir(imagesDir);
        if (!dir) {
            printf("No images found\n");
            return 0;
        }
        printf("NAME\\tTAG\\tID\\tCREATED\n");
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (!strstr(entry->d_name, ".json")) continue;
            char name[100], tag[100];
            if (sscanf(entry->d_name, "%[^_]_%[^.]", name, tag) == 2) {
                printf("%s\\t%s\\t...\\t...\\n", name, tag);
            }
        }
        closedir(dir);
        return 0;
    }
    
    else if (strcmp(argv[1], "rmi") == 0) {
        if (argc < 3) {
            printf("Usage: docksmith rmi <name:tag>\n");
            return 1;
        }
        char imageName[100], imageTag[100];
        char *imageRef = argv[2];
        char *colon = strchr(imageRef, ':');
        if (colon) {
            strncpy(imageName, imageRef, colon - imageRef);
            imageName[colon - imageRef] = '\0';
            strcpy(imageTag, colon + 1);
        } else {
            strcpy(imageName, imageRef);
            strcpy(imageTag, "latest");
        }
        
        char manifestPath[512];
        sprintf(manifestPath, "%s/.docksmith/images/%s_%s.json", getenv("HOME"), imageName, imageTag);
        if (remove(manifestPath) == 0) {
            printf("Deleted %s:%s\n", imageName, imageTag);
        } else {
            printf("Image not found\n");
            return 1;
        }
        return 0;
    }
    
    else {
        printf("Unknown command: %s\n", argv[1]);
        printf("Usage: docksmith build -t <name:tag> <context>\n");
        return 1;
    }
}