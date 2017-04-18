using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;
using System.Collections;

#if UNITY_EDITOR
using UnityEditor;
#endif


namespace UTJ
{
    [AddComponentMenu("UTJ/Hair Works Integration/Hair Instance")]
    [ExecuteInEditMode]
    public class HairInstance : MonoBehaviour
    {
        #region static
        static HashSet<HairInstance> s_instances;
        static int s_nth_OnWillRenderObject;

        static CommandBuffer s_command_buffer;
        static CommandBuffer s_command_bufferVR;
        static CommandBuffer s_command_bufferVR_singlePass;
        static HashSet<Camera> s_cameras = new HashSet<Camera>();

        static public HashSet<HairInstance> GetInstances()
        {
            if (s_instances == null)
            {
                s_instances = new HashSet<HairInstance>();
            }
            return s_instances;
        }
        #endregion

        static CameraEvent s_timing             = CameraEvent.BeforeImageEffects;
        static CameraEvent s_timingSceneCamera  = CameraEvent.AfterForwardOpaque;

        private const int MAX_NUM_HAIR_TEXTURES = 14;
        public Transform m_root_bone;
        public string m_hair_asset; 
        public string m_hair_shader         = "UTJ/HairWorksIntegration/DefaultHairShader.cso";
        public bool m_invert_bone_x         = true;
        public bool use_default_parameters  = true;

        private bool hairTexturesAssigned   = false;

        public hwi.Descriptor m_params          = hwi.Descriptor.default_value;
        private hwi.Descriptor m_paramsProxy    = hwi.Descriptor.default_value;
        private bool m_saved_default_params     = false;
        hwi.HShader m_hshader                   = hwi.HShader.NullHandle;
        hwi.HAsset m_hasset                     = hwi.HAsset.NullHandle;
        hwi.HInstance m_hinstance               = hwi.HInstance.NullHandle;

        public Transform[] m_bones;
        Matrix4x4[] m_inv_bindpose;
        Matrix4x4[] m_skinning_matrices;
        Matrix4x4[] m_bindpose;
        IntPtr m_skinning_matrices_ptr;
        Matrix4x4 m_conversion_matrix;

        // texture used by this instance
        Texture2D [] m_hairTextures = new Texture2D[MAX_NUM_HAIR_TEXTURES];
        String[] m_hairTextureNames = new String[MAX_NUM_HAIR_TEXTURES];

        private bool m_hairSystemStarted = false;

        public uint shader_id   { get { return m_hshader; } }
        public uint asset_id    { get { return m_hasset; } }
        public uint instance_id { get { return m_hinstance; } }

        /// <summary>
        /// wait until the hair system has been flushed
        /// </summary>
        /// <returns></returns>
        private bool WaitForHairSystemFlush()
        {
            return false;
        }

        void RepaintWindow()
        {
#if UNITY_EDITOR
            var assembly    = typeof(UnityEditor.EditorWindow).Assembly;
            var type        = assembly.GetType("UnityEditor.GameView");
            var gameview    = EditorWindow.GetWindow(type);
            gameview.Repaint();
#endif
        }

        public void LoadHairShader(string path_to_cso)
        {
            // release existing shader
            if (m_hshader)
            {
                hwi.hwShaderRelease(m_hshader);
                m_hshader = hwi.HShader.NullHandle;
            }

            // load shader
            //if (m_hshader = hwi.hwShaderLoadFromFile(Application.streamingAssetsPath + "/" + path_to_cso))
            if (m_hshader = hwi.hwShaderLoadFromFile(HairResourcesPath() + path_to_cso))
            {
                m_hair_shader = path_to_cso;
            }

#if UNITY_EDITOR
            RepaintWindow();
#endif
        }

        public void ReloadHairShader()
        {
            hwi.hwShaderReload(m_hshader);
            RepaintWindow();
        }

        public void LoadHairAsset(string path_to_apx, bool reset_params = true)
        {
            // release existing instance & asset
            if (m_hinstance)
            {
                hwi.hwInstanceRelease(m_hinstance);
                m_hinstance = hwi.HInstance.NullHandle;
            }

            if (m_hasset)
            {
                hwi.hwAssetRelease(m_hasset);
                m_hasset = hwi.HAsset.NullHandle;
            }

            // load & create instance
            m_hasset = hwi.hwAssetLoadFromFile(HairResourcesPath() + path_to_apx);
            if (m_hasset.id == hwi.HAsset.NullHandle)
                return;

            if (m_hasset)
            {
                m_hair_asset = path_to_apx;
                m_hinstance = hwi.hwInstanceCreate(m_hasset);
                if (reset_params)
                {
                    hwi.hwAssetGetDefaultDescriptor(m_hasset, ref m_params);
                }
            }

            // update bone structure
            if (reset_params)
            {
                m_bones = null;
                m_skinning_matrices = null;
                m_skinning_matrices_ptr = IntPtr.Zero;
            }

            // update the bones
            UpdateBones();

            // get the names for the hair textures used by this asset
            GetHairTextureNames();

            // prepare all the needed textures
            LoadHairTextures(Path.GetDirectoryName(path_to_apx));

#if UNITY_EDITOR
            RepaintWindow();
#endif
        }

        public void ReloadHairAsset()
        {
            hwi.hwAssetReload(m_hasset);
            hwi.hwAssetGetDefaultDescriptor(m_hasset, ref m_params);
            hwi.hwInstanceSetDescriptor(m_hinstance, ref m_params);
            RepaintWindow();
        }

        public void AssignTexture(hwi.TextureType type, Texture2D tex)
        {
            hwi.hwInstanceSetTexture(m_hinstance, type, tex.GetNativeTexturePtr());
        }

        public void AssignTextureToDevice(hwi.TextureType type)
        {
            hwi.hwInstanceSetTextureIntoDevice(m_hinstance, type);
        }

        /// <summary>
        /// Update bones for hair
        /// </summary>
        public void UpdateBones()
        {
            int num_bones = hwi.hwAssetGetNumBones(m_hasset);

            // try to find the skeleton in the Unity hierarchy
            if (m_bones == null || m_bones.Length != num_bones)
            {
                m_bones = new Transform[num_bones];

                if (m_root_bone == null)
                {
                    m_root_bone = GetComponent<Transform>();
                }

                var children = m_root_bone.GetComponentsInChildren<Transform>();
                for (int i = 0; i < num_bones; ++i)
                {
                    string name = hwi.hwAssetGetBoneNameString(m_hasset, i);
                    m_bones[i] = Array.Find(children, (a) => { return a.name == name; });
                    if (m_bones[i] == null) { m_bones[i] = m_root_bone; }
                }
            }

            // prepare all the arrays with bones/matrices
            if (m_skinning_matrices == null) 
            {
                m_inv_bindpose              = new Matrix4x4[num_bones];
                m_skinning_matrices         = new Matrix4x4[num_bones];
                m_bindpose                  = new Matrix4x4[num_bones];

                m_skinning_matrices_ptr     = Marshal.UnsafeAddrOfPinnedArrayElement(m_skinning_matrices, 0);
                for (int i = 0; i < num_bones; ++i)
                {
                    m_inv_bindpose[i] = Matrix4x4.identity;
                    m_skinning_matrices[i] = Matrix4x4.identity;
                    m_bindpose[i] = Matrix4x4.identity;
                }

                for (int i = 0; i < num_bones; ++i)
                {
                    hwi.hwAssetGetBindPose(m_hasset, i, ref m_bindpose[i]);
                    m_inv_bindpose[i] = m_bindpose[i].inverse;
                }
                m_conversion_matrix = Matrix4x4.identity;
                if (m_invert_bone_x)
                {
                    m_conversion_matrix *= Matrix4x4.Scale(new Vector3(-1.0f, 1.0f, 1.0f));
                }
            }

            for (int i = 0; i < m_bones.Length; ++i)
            {
                var t = m_bones[i];
                if (t != null)
                {
                    m_skinning_matrices[i] = t.localToWorldMatrix * m_conversion_matrix; 
                    // original m_skinning_matrices[i] = t.localToWorldMatrix * m_conversion_matrix * m_inv_bindpose[i];
                }
            }
        }

        public void FinishedRenderingHair()
        {
            s_nth_OnWillRenderObject = 0;
        }

        /// <summary>
        /// Swap two values
        /// </summary>
        /// <typeparam name="T"></typeparam>
        /// <param name="a"></param>
        /// <param name="b"></param>
        static public void Swap<T>(ref T a, ref T b)
        {
            T tmp = a;
            a = b;
            b = tmp;
        }

#if UNITY_EDITOR
        void Reset()
        {
            var skinned_mesh_renderer = GetComponent<SkinnedMeshRenderer>();
            m_root_bone = skinned_mesh_renderer != null ? skinned_mesh_renderer.rootBone : GetComponent<Transform>();
        }
#endif

        void OnApplicationQuit()
        {
            // pause the the C++ rendering for few frames as we're shutting down
            hwi.hwSetShuttingDownFlag();

            // start the wait for the hair system
            ClearCommandBuffer();
        }

        void Awake()
        {
           
#if UNITY_EDITOR
                if (!hwi.hwLoadHairWorks())
                {
                    EditorUtility.DisplayDialog(
                        "Hair Works Integration",
                        "Failed to load HairWorks (version " + hwi.hwGetSDKVersion() + ") dll. You need to get HairWorks SDK from NVIDIA website. Read document for more detail.",
                        "OK");
                }
#endif
                hwi.hwSetLogCallback();

                // reset bones here
                m_bones = null;
        }

        void OnDestroy()
        {
            hwi.hwInstanceRelease(m_hinstance);
            hwi.hwAssetRelease(m_hasset);
        }

        void OnEnable()
        {
            GetInstances().Add(this);
            m_params.m_enable   = true;
            m_hairSystemStarted = false;
        }

        void OnDisable()
        {
            m_params.m_enable = false;
            GetInstances().Remove(this);
        }

        void Start()
        {
            LoadHairShader(m_hair_shader);
            LoadHairAsset(m_hair_asset, false);
            m_hairSystemStarted = true;
        }

        void LateUpdate()
        {
            UpdateBones();
        }

        public void HairRendering()
        {
            if (!m_hairSystemStarted)
                return;

            if (!m_hasset)
                return;

            if (m_saved_default_params == false)
            {
                hwi.hwInstanceGetDescriptor(m_hinstance, ref m_paramsProxy);
                m_saved_default_params = true;
            }

            // force gravity down the Y axes
            m_params.m_gravityDir.x = 0.0f;
            m_params.m_gravityDir.y = -1.0f;
            m_params.m_gravityDir.z = 0.0f;

            if (m_saved_default_params == true)
            {
                hwi.hwInstanceSetDescriptor(m_hinstance, ref m_params);
            }

            RenderEntrypoint();
        }

        // simulation
        private void StepSimulation(Camera cam)
        {
            if (cam != null)
            {
                if (cam.stereoEnabled == true)
                {
                    bool singlePassVR = SinglePassVRRendering();
                    hwi.hwStepSimulation(Time.deltaTime, true, singlePassVR);
                }
                else
                {
                    hwi.hwStepSimulation(Time.deltaTime, false, false);
                }
            }
            else
            {
                hwi.hwStepSimulation(Time.deltaTime, false, false);
            }
        }

        // the main entrypoing for hair rendering
        public void RenderEntrypoint()
        {
            if (!m_hairSystemStarted)
                return;

            var cam = Camera.current;
            if (cam.name == "Preview Camera") 
            {
                if (cam.stereoEnabled == true)
                    return;
            }

            // check if this is a stereo rendering camera
            bool vrMode = Camera.current.stereoEnabled;
             
            if (s_nth_OnWillRenderObject++ == 0)
            {
                BeginRender(vrMode);

                // submit bones/skinning to hairworks
                foreach (var a in GetInstances())
                {
                    a.SetBonesMatrices(vrMode);
                }

                // submit simulation step to hairworks
                StepSimulation(Camera.current);

                // submit actualt rendering
                foreach (var b in GetInstances())
                {
                    b.Render(vrMode);
                }

                EndRender(vrMode);
            }
        }

        // are we using deferred rendering
        static public bool IsDeferred(Camera cam)
        {
            if (cam.renderingPath == RenderingPath.DeferredShading
#if UNITY_EDITOR
            || (cam.renderingPath == RenderingPath.UsePlayerSettings && PlayerSettings.renderingPath == RenderingPath.DeferredShading)
#endif
            )
            {
                return true;
            }
            return false;
        }

        static public bool DoesRenderToTexture(Camera cam)
        {
            if (IsUnityVersion("5.6"))
            {
                return true;
            }

            if (UnityEngine.VR.VRSettings.enabled == true)
            {
                return true;
            }
            else
            {
                return IsDeferred(cam) || cam.targetTexture != null;
            }
        }

        static public void ClearCommandBuffer()
        {
            foreach (var c in s_cameras)
            {
                if (c != null)
                {
                    c.RemoveCommandBuffer(s_timing, s_command_buffer);
                }
            }
            s_cameras.Clear();
        }

        //
        private void BeginRender(bool vrMode)
        {
            if (!m_hairSystemStarted)
                return;

            if (s_command_buffer == null)
            {
                s_command_buffer = new CommandBuffer();
                s_command_buffer.name = "Hair";
                s_command_buffer.IssuePluginEvent(hwi.hwGetRenderEventFunc(), 0);
            }

            if (s_command_bufferVR == null)
            {
                s_command_bufferVR = new CommandBuffer();
                s_command_bufferVR.name = "HairVR";
                s_command_bufferVR.IssuePluginEvent(hwi.hwGetRenderEventFunc(), 1);
            }

            if (s_command_bufferVR_singlePass == null)
            {
                s_command_bufferVR_singlePass = new CommandBuffer();
                s_command_bufferVR_singlePass.name = "HairVRSinglePass";
                s_command_bufferVR_singlePass.IssuePluginEvent(hwi.hwGetRenderEventFunc(), 2);
            }

            Camera cam = Camera.current;

            if (cam != null)
            {
                // set camera and projection matrix for hairworks
                SetCameraProjectionMatrix(cam);

                // set light data
                HairLight.AssignLightData(vrMode);

                if (!s_cameras.Contains(cam))
                {
                    AddCameraCommandBuffer(cam);
                    s_cameras.Add(cam);
                }
                else
                {
                    UpdateCameraCommandBuffer(cam);
                }

            }

            hwi.hwBeginScene(vrMode);
        }

        //
        void SetBonesMatrices(bool vrMode)
        {
            hwi.hwInstanceUpdateSkinningMatricesAsync(m_hinstance, m_skinning_matrices.Length, m_skinning_matrices_ptr, vrMode);
        }

        //
        void Render(bool vrMode)
        {
            if (!m_hairSystemStarted)
                return;

            if (!m_hasset)
                return; 

            hwi.hwSetShader(m_hshader, vrMode);

            if (hairTexturesAssigned == false)
            {
                hairTexturesAssigned = true;
                SetHairTexturesForRendering();
            }

            // render
            hwi.hwRender(m_hinstance, vrMode);
        }

        //
        private void EndRender(bool vrMode)
        {
            if (!m_hairSystemStarted)
                return;

            hwi.hwEndScene(vrMode);
        }

        //
        void GetHairTextureNames()
        {
            m_hairTextureNames = new String[MAX_NUM_HAIR_TEXTURES];
            for (int i = 0; i < MAX_NUM_HAIR_TEXTURES; ++i)
            {
                String textureName = hwi.hwAssetGetTextureNameString(m_hasset, i);
                if (String.IsNullOrEmpty(textureName))
                    m_hairTextureNames[i] = null;
                else
                    m_hairTextureNames[i] = textureName;
            }
        }

        // load all the needed hair textures
        void LoadHairTextures(String pathToTexture)
        {
            for (int i = 0; i < MAX_NUM_HAIR_TEXTURES; ++i)
            {
                String textureName = m_hairTextureNames[i];
                if (String.IsNullOrEmpty(textureName))
                    m_hairTextures[i] = null;
                else
                    m_hairTextures[i] = LoadHairTexture(textureName);
            }

        }
    
        // load one hair texture
        Texture2D LoadHairTexture(String textureFile)
        {
            String fileNameNoExtension = Path.GetFileNameWithoutExtension(textureFile);
            Texture2D retTexture        = (Texture2D) Resources.Load("HairWorks" + "/" + fileNameNoExtension); 
            return retTexture;
        }

        // set the hair textures in the rendering pipeline
        void SetHairTexturesForRendering()
        {
            for (int i = 0; i< MAX_NUM_HAIR_TEXTURES; ++i)
            {
                if (m_hairTextures[i] != null)
                {
                    if(i == (int)hwi.TextureType.ROOT_COLOR || i ==(int) hwi.TextureType.TIP_COLOR || i == (int)hwi.TextureType.DENSITY)
                    AssignTexture((hwi.TextureType) i,  m_hairTextures[i]);
                }
            }
        }

        /// <summary>
        /// Add a new command buffer to the camera
        /// </summary>
        /// <param name="cam"></param>
        void AddCameraCommandBuffer(Camera cam)
        {
            if (cam.stereoEnabled == true)
            {
                if (SinglePassVRRendering() == true)
                {
                    cam.AddCommandBuffer(s_timing, s_command_bufferVR_singlePass);
                }
                else
                {
                    cam.AddCommandBuffer(s_timing, s_command_bufferVR);
                }
            }
            else
            {
                if (IsUnitySceneCamera(cam) && IsUnityVersion("5.6"))
                {
                    // Unity's scene camera has to use different timing because don't do after effects
                    cam.AddCommandBuffer(s_timingSceneCamera, s_command_buffer);
                }
                else
                {
                    cam.AddCommandBuffer(s_timing, s_command_buffer);
                }
            }
        }


        /// <summary>
        /// Update camera's command buffer based on camera being VR or not
        /// </summary>
        /// <param name="cam"></param>
        void UpdateCameraCommandBuffer(Camera cam)
        {
            CommandBuffer[] camBuffer = cam.GetCommandBuffers(s_timing);
            if (camBuffer == null || camBuffer.Length == 0)
                return;
            if (camBuffer.Length > 1)
                throw new UnityException("UpdateCameraCommandBudder(): camera has more than one command buffers!");

            if (cam.stereoEnabled == true)
            {
                if (SinglePassVRRendering())
                {
                    if (camBuffer[0].name != "HairVRSinglePass")
                    {
                        cam.RemoveAllCommandBuffers();
                        cam.AddCommandBuffer(s_timing, s_command_bufferVR_singlePass);
                    }
                }
                else
                {
                    if (camBuffer[0].name != "HairVR")
                    {
                        cam.RemoveAllCommandBuffers();
                        if (IsUnitySceneCamera(cam) && IsUnityVersion("5.6"))
                        {
                            // Unity's scene camera has to use different timing because don't do after effects
                            cam.AddCommandBuffer(s_timingSceneCamera, s_command_bufferVR);
                        }
                        else
                        {
                            cam.AddCommandBuffer(s_timing, s_command_bufferVR);
                        }
                    }
                }
            }
            else
            {
                if (camBuffer[0].name != "Hair")
                {
                    cam.RemoveAllCommandBuffers();
                    cam.AddCommandBuffer(s_timing, s_command_buffer);
                }
            }
        }

        /// <summary>
        /// Get the 
        /// </summary>
        /// <param name="textureName"></param>
        /// <returns></returns>
        String GetHairTexturePath(String textureName)
        {
            return String.Empty;
        }

        /// <summary>
        /// Path to hair resources
        /// </summary>
        /// <returns></returns>
        private string HairResourcesPath()
        {
            return Application.dataPath + "/StreamingAssets/";
        }

        /// <summary>
        /// Tell if this camera is a Unity scnene camera
        /// </summary>
        /// <param name="cam"></param>
        /// <returns></returns>
        private bool IsUnitySceneCamera(Camera cam)
        {
            if (cam.name == "SceneCamera")
                return true;
            else
                return false;
        }

        private void SetCameraProjectionMatrix(Camera cam)
        {
            bool isStereo   = cam.stereoEnabled;
            float fov       = cam.fieldOfView * cam.aspect;

            if (isStereo)
            {
                bool singlePassStereoRender = SinglePassVRRendering();
                
                Matrix4x4 Vl = cam.GetStereoViewMatrix(Camera.StereoscopicEye.Left);
                Matrix4x4 Pl = GL.GetGPUProjectionMatrix(cam.GetStereoProjectionMatrix(Camera.StereoscopicEye.Left), DoesRenderToTexture(cam));

                Matrix4x4 Vr = cam.GetStereoViewMatrix(Camera.StereoscopicEye.Right);
                Matrix4x4 Pr = GL.GetGPUProjectionMatrix(cam.GetStereoProjectionMatrix(Camera.StereoscopicEye.Right), DoesRenderToTexture(cam));
         
                hwi.hwSetViewProjectionStereo(ref Vl, ref Pl, ref Vr, ref Pr, fov, singlePassStereoRender);
                
            }
            else
            {
                Matrix4x4 V = cam.worldToCameraMatrix;
                Matrix4x4 P = GL.GetGPUProjectionMatrix(cam.projectionMatrix, DoesRenderToTexture(cam));
                hwi.hwSetViewProjection(ref V, ref P, fov);
            }
        }

        /// <summary>
        /// Check unity version 
        /// </summary>
        /// <returns></returns>
        private static bool IsUnityVersion(string version)
        {
            if (Application.unityVersion.Contains(version))
                return true;
            else
                return false;
        }


        //
        private bool SinglePassVRRendering()
        {
#if UNITY_EDITOR
            // carloX return (PlayerSettings.stereoRenderingPath == StereoRenderingPath.SinglePass);
            return true;
    #else
            return true;
   #endif
        }

        //
        static Matrix4x4 HairworksCamera(Camera cam)
        {
            Matrix4x4 retMatrix = new Matrix4x4();
            Vector3 cameraPosition  = cam.transform.position;
            Vector3 cameraUp        = cam.transform.up;
            Vector3 cameraFwd       = cam.transform.forward;

            // normalize and set position for the camera
            cameraUp.Normalize();
            cameraFwd.Normalize();
                  
            // negate position
            Vector3 negCameraPosition = -cameraPosition;

            Vector3 cameraRight = Vector3.Cross(cameraUp, cameraFwd);
            cameraRight.Normalize();

            cameraUp = Vector3.Cross(cameraFwd, cameraRight); 
            cameraUp.Normalize();
            // dot 
            float D0Dot = Vector3.Dot(cameraRight, negCameraPosition);
            float D1Dot = Vector3.Dot(cameraUp, negCameraPosition);
            float D2Dot = Vector3.Dot(cameraFwd, negCameraPosition);

            Vector4 row0 = new Vector4(cameraRight.x, cameraRight.y, cameraRight.z, D0Dot);
            Vector4 row1 = new Vector4(cameraUp.x, cameraUp.y, cameraUp.z, D1Dot);
            Vector4 row2 = new Vector4(cameraFwd.x, cameraFwd.y, cameraFwd.z, D2Dot);


            // setup the final matrix
            
            retMatrix.m00 = row0.x;
            retMatrix.m01 = row0.y;
            retMatrix.m02 = row0.z;
            retMatrix.m03 = row0.w;

            retMatrix.m10 = row1.x;
            retMatrix.m11 = row1.y;
            retMatrix.m12 = row1.z;
            retMatrix.m13 = row1.w;

            retMatrix.m20 = row2.x;
            retMatrix.m21 = row2.y;
            retMatrix.m22 = row2.z;
            retMatrix.m23 = row2.w;

            retMatrix.m30 = 0.0f;
            retMatrix.m31 = 0.0f;
            retMatrix.m32 = 0.0f;
            retMatrix.m33 = 1.0f;

            //
            return retMatrix;
        }

        //
        static Matrix4x4 HairworksProjection(float fov_vert, float AR, float near_plane, float far_plane)
        {
            Matrix4x4 retMatrix = new Matrix4x4();
            float h, w, Q;
            float fov_vert_rad  = Mathf.Deg2Rad * fov_vert;

            h = 1.0f / Mathf.Tan((fov_vert_rad  * 0.5f));    // 1/tan(x) == cot(x)
            w = h / AR;
            Q = far_plane / (far_plane - near_plane);

            retMatrix = Matrix4x4.identity;

            retMatrix.m00 = w;
            retMatrix.m11 = h;
            retMatrix.m22 = Q;

            retMatrix.m23 = -Q * near_plane;    
            retMatrix.m32 = 1;

            retMatrix.m33 = 0.0f;

            return retMatrix;
        }

        

    }
}
