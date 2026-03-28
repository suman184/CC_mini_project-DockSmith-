#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef struct {
    char name[100];
    char workingDir[100];

    char envKeys[10][100];
    char envValues[10][100];
    int envCount;

    char cmd[200];   // 🔥 ADD THIS

} Image;

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
            if (count == 1) {
                args[0] = '\0';
            }

            printf("Command: %s | Args: %s\n", command, args);

            // 🔥 DISPATCHER
            // FROM blokc
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
            }
            else if (strcmp(command, "COPY") == 0) {
                printf("-> Handling COPY\n");
            }
            else if (strcmp(command, "RUN") == 0) {
                printf("-> Handling RUN\n");
            }
            else if (strcmp(command, "WORKDIR") == 0) {
                printf("-> Handling WORKDIR\n");
                strcpy(currentImage.workingDir, args);
                printf("✅ WorkingDir set to: %s\n", currentImage.workingDir);
            }
            else if (strcmp(command, "ENV") == 0) {
                printf("-> Handling ENV\n");

                char key[100], value[100];
                sscanf(args, "%[^=]=%s", key, value);

                strcpy(currentImage.envKeys[currentImage.envCount], key);
                strcpy(currentImage.envValues[currentImage.envCount], value);

                currentImage.envCount++;

                printf("✅ ENV added: %s=%s\n", key, value);
            }
            else if (strcmp(command, "CMD") == 0) {
                printf("-> Handling CMD\n");

                // simple extraction (not perfect JSON parsing)
                char temp[200];
                strcpy(temp, args);

                // remove [ ]
                temp[strlen(temp)-1] = '\0';  // remove ]
                memmove(temp, temp+1, strlen(temp));  // remove [

                // remove quotes and commas → make it simple
                for (int i = 0; temp[i]; i++) {
                    if (temp[i] == '"' || temp[i] == ',') {
                        temp[i] = ' ';
                    }
                }

                strcpy(currentImage.cmd, temp);

                printf("✅ CMD set to: %s\n", currentImage.cmd);
            }            
            else {
                printf("❌ Unknown instruction: %s\n", command);
                return 1;
            }
        }
        printf("\nFinal Image State:\n");
        printf("Name: %s\n", currentImage.name);
        printf("WorkingDir: %s\n", currentImage.workingDir);
        printf("\nEnvironment Variables:\n");
        for (int i = 0; i < currentImage.envCount; i++) {
            printf("%s=%s\n",
                currentImage.envKeys[i],
                currentImage.envValues[i]);
        }
        printf("CMD: %s\n", currentImage.cmd);
        fclose(fp);
        return 0;  // 🔥 MUST STOP HERE
    }
}
