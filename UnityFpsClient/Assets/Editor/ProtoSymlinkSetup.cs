using System;
using System.Diagnostics;
using System.IO;
using UnityEditor;
using UnityEngine;

[InitializeOnLoad]
public class ProtoSymlinkSetup
{
    static ProtoSymlinkSetup()
    {
        // 유니티 프로젝트 내부 가상 폴더 경로 (Assets/Protos)
        string targetPath = Path.Combine(Application.dataPath, "Protos");
        
        // 유니티 프로젝트 외부 실제 proto 폴더 경로 (project/proto)
        string sourcePath = Path.GetFullPath(Path.Combine(Application.dataPath, "../../proto"));

        // 만약 실제 proto 폴더가 존재하지 않는다면 링크 생성을 진행하지 않음
        if (!Directory.Exists(sourcePath))
        {
            UnityEngine.Debug.LogWarning($"[ProtoSymlink] 원본 proto 폴더를 찾을 수 없어 링크 생성을 건너뜁니다: {sourcePath}");
            return;
        }

        // 링크 폴더가 존재하지 않을 때만 생성 진행
        if (!Directory.Exists(targetPath))
        {
            CreateSymlink(sourcePath, targetPath);
        }
    }

    private static void CreateSymlink(string source, string target)
    {
        try
        {
            if (Application.platform == RuntimePlatform.WindowsEditor)
            {
                // Windows: Directory Junction (/J) 생성
                ProcessStartInfo startInfo = new ProcessStartInfo
                {
                    FileName = "cmd.exe",
                    Arguments = $"/c mklink /J \"{target}\" \"{source}\"",
                    CreateNoWindow = true,
                    UseShellExecute = false
                };
                using (Process process = Process.Start(startInfo))
                {
                    process?.WaitForExit();
                }
            }
            else if (Application.platform == RuntimePlatform.OSXEditor || Application.platform == RuntimePlatform.LinuxEditor)
            {
                // macOS / Linux: Symlink (ln -s) 생성
                ProcessStartInfo startInfo = new ProcessStartInfo
                {
                    FileName = "ln",
                    Arguments = $"-s \"{source}\" \"{target}\"",
                    CreateNoWindow = true,
                    UseShellExecute = false
                };
                using (Process process = Process.Start(startInfo))
                {
                    process?.WaitForExit();
                }
            }

            UnityEngine.Debug.Log($"[ProtoSymlink] 외부 proto 폴더 링크 생성 완료: {target} -> {source}");
            AssetDatabase.Refresh();
        }
        catch (Exception e)
        {
            UnityEngine.Debug.LogError($"[ProtoSymlink] 가상 폴더 생성 중 예외 발생: {e.Message}");
        }
    }
}
