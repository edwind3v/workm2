// Standalone DX9Ex offscreen check. Uses Granny's actual CPU deformer as the
// reference and renders the same vertices through the production skinning VS.
// No game login, assets, or visible window is required.
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <d3dcompiler.h>
#include <granny.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "../EterGrnLib/GPUSkinningShader.h"

void Check(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        std::printf("FAIL %s hr=0x%08x\n", operation, static_cast<unsigned>(result));
        throw std::runtime_error(operation);
    }
}
struct Vertex
{
    float position[3]; BYTE weights[4], indices[4]; float normal[3], uv[2], uv1[2];
};
struct DrawConstants
{
    D3DXMATRIX wvp, worldView;
    D3DXVECTOR4 normals[3];
    D3DXMATRIX texture[2];
    D3DXVECTOR4 initial, fog, flags, modes, reserved, lights[12], literalSpace[4];
};
static_assert(sizeof(Vertex) == 48);
static_assert(sizeof(DrawConstants) == 640);

int main()
try
{
    HWND window = CreateWindowExA(0, "STATIC", "Skinning check", WS_POPUP,
        0, 0, 128, 128, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!window) throw std::runtime_error("CreateWindowEx");
    IDirect3D9Ex* api = nullptr;
    Check(Direct3DCreate9Ex(D3D_SDK_VERSION, &api), "Direct3DCreate9Ex");
    D3DPRESENT_PARAMETERS presentation{};
    presentation.Windowed = TRUE; presentation.SwapEffect = D3DSWAPEFFECT_DISCARD;
    presentation.hDeviceWindow = window; presentation.BackBufferFormat = D3DFMT_UNKNOWN;
    IDirect3DDevice9Ex* device = nullptr;
    Check(api->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING, &presentation, nullptr, &device), "CreateDeviceEx");
    ID3DBlob *bytecode = nullptr, *errors = nullptr;
    HRESULT compiled = D3DCompile(GPUSkinningShaderSource, sizeof(GPUSkinningShaderSource) - 1,
        "GPU_skinning", nullptr, nullptr, "main", "vs_3_0", D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0, &bytecode, &errors);
    if (errors) { std::printf("%s", static_cast<const char*>(errors->GetBufferPointer())); errors->Release(); }
    Check(compiled, "D3DCompile");
    IDirect3DVertexShader9* shader = nullptr;
    Check(device->CreateVertexShader(static_cast<const DWORD*>(bytecode->GetBufferPointer()), &shader), "CreateVertexShader");
    bytecode->Release();
    const D3DVERTEXELEMENT9 elements[] = {
        {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0},
        {0,16,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0},
        {0,20,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
        {0,32,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},
        {0,40,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()
    };
    IDirect3DVertexDeclaration9* declaration = nullptr;
    Check(device->CreateVertexDeclaration(elements, &declaration), "CreateVertexDeclaration");
    std::array<D3DXMATRIX,72> bones;
    std::array<granny_int32x,72> mapping;
    D3DXVECTOR4 palette[216];
    for (unsigned b = 0; b < 72; ++b)
    {
        D3DXMATRIX rotation, translation;
        D3DXMatrixRotationYawPitchRoll(&rotation, 0.002f*b, -0.001f*b, 0.003f*b);
        D3DXMatrixTranslation(&translation, 0.001f*b, -0.0007f*b, 0);
        bones[b] = rotation * translation;
        mapping[b] = 71-b;
    }
    for (unsigned b = 0; b < 72; ++b)
        for (unsigned col = 0; col < 3; ++col)
        {
            auto& m = bones[mapping[b]];
            palette[b*3+col] = D3DXVECTOR4(m(0,col),m(1,col),m(2,col),m(3,col));
        }
    Vertex input[3] = {
        {{-0.75f,-0.7f,0.4f},{64,64,64,63},{0,1,2,71},{0.2f,0.3f,-0.9327379f},{0,0},{0,0}},
        {{0,0.75f,0.4f},{0,255,0,0},{0,71,0,0},{0.2f,0.3f,-0.9327379f},{0.5f,1},{0.5f,1}},
        {{0.7f,-0.65f,0.4f},{127,128,0,0},{2,71,0,0},{0.2f,0.3f,-0.9327379f},{1,0},{1,0}}
    };
    granny_data_type_definition type[] = {
        {GrannyReal32Member,GrannyVertexPositionName,nullptr,3},
        {GrannyNormalUInt8Member,GrannyVertexBoneWeightsName,nullptr,4},
        {GrannyUInt8Member,GrannyVertexBoneIndicesName,nullptr,4},
        {GrannyReal32Member,GrannyVertexNormalName,nullptr,3},
        {GrannyReal32Member,GrannyVertexTextureCoordinatesName "0",nullptr,2},
        {GrannyReal32Member,GrannyVertexTextureCoordinatesName "1",nullptr,2},{GrannyEndMember}
    };
    granny_pwnt3432_vertex source[3];
    for (unsigned v=0;v<3;++v) std::memcpy(&source[v],&input[v],sizeof(source[v]));
    granny_vertex_data data{};
    data.VertexType=GrannyPWNT3432VertexType; data.VertexCount=3;
    data.Vertices=reinterpret_cast<granny_uint8*>(source);
    granny_mesh mesh{}; mesh.PrimaryVertexData=&data;
    Vertex converted[3]{};
    GrannyCopyMeshVertices(&mesh,type,converted);
    for(unsigned v=0;v<3;++v)
        if(std::memcmp(&converted[v],&source[v],sizeof(source[v])))
            throw std::runtime_error("GrannyCopyMeshVertices changed four-weight input");
    std::memcpy(input,converted,sizeof(input));
    std::printf("PASS Granny four-weight source conversion\n");
    granny_pwnt3232_vertex twoWeights{};
    std::memcpy(twoWeights.Position,source[0].Position,sizeof(twoWeights.Position));
    std::memcpy(twoWeights.Normal,source[0].Normal,sizeof(twoWeights.Normal));
    std::memcpy(twoWeights.UV,source[0].UV,sizeof(twoWeights.UV));
    twoWeights.BoneWeights[0]=127; twoWeights.BoneWeights[1]=128;
    twoWeights.BoneIndices[0]=0; twoWeights.BoneIndices[1]=71;
    data.VertexType=GrannyPWNT3232VertexType; data.VertexCount=1;
    data.Vertices=reinterpret_cast<granny_uint8*>(&twoWeights);
    Vertex widened{};
    GrannyCopyMeshVertices(&mesh,type,&widened);
    if(widened.weights[0]!=127 || widened.weights[1]!=128 || widened.weights[2] || widened.weights[3] ||
        widened.indices[0]!=0 || widened.indices[1]!=71)
        throw std::runtime_error("Granny two-weight widening changed weights or indices");
    std::printf("PASS Granny two-weight source conversion\n");
    // Verify that the binding count is the mesh palette count, and linked
    // equipment can remap that palette into a differently ordered skeleton.
    granny_bone sourceBones[4]{}, targetBones[4]{};
    const char* names[]={"root","spine","arm","hand"};
    for(unsigned b=0;b<4;++b)
    {
        sourceBones[b].Name=names[b]; targetBones[b].Name=names[3-b];
        sourceBones[b].ParentIndex=targetBones[b].ParentIndex=-1;
        GrannyMakeIdentity(&sourceBones[b].LocalTransform);
        GrannyMakeIdentity(&targetBones[b].LocalTransform);
    }
    granny_skeleton sourceSkeleton{},targetSkeleton{};
    sourceSkeleton.BoneCount=targetSkeleton.BoneCount=4;
    sourceSkeleton.Bones=sourceBones; targetSkeleton.Bones=targetBones;
    granny_bone_binding bindings[2]{};
    bindings[0].BoneName=names[1]; bindings[1].BoneName=names[3];
    granny_mesh bindingMesh{}; bindingMesh.BoneBindingCount=2; bindingMesh.BoneBindings=bindings;
    for(unsigned linked=0;linked<2;++linked)
    {
        auto* binding=GrannyNewMeshBinding(&bindingMesh,&sourceSkeleton,linked?&targetSkeleton:&sourceSkeleton);
        if(!binding) throw std::runtime_error("GrannyNewMeshBinding");
        const auto* mapped=GrannyGetMeshBindingToBoneIndices(binding);
        const bool ok=GrannyGetMeshBindingBoneCount(binding)==2 && mapped &&
            mapped[0]==(linked?2:1) && mapped[1]==(linked?0:3);
        GrannyFreeMeshBinding(binding);
        if(!ok) throw std::runtime_error("Granny mesh palette mapping/count");
    }
    std::printf("PASS Granny default and linked skeleton palette mapping\n");
    granny_mesh_deformer* deformer = GrannyNewMeshDeformer(GrannyPWNT3432VertexType, GrannyPNT332VertexType,
        GrannyDeformPositionNormal, GrannyDontAllowUncopiedTail);
    if (!deformer) throw std::runtime_error("GrannyNewMeshDeformer");
    granny_pnt332_vertex cpu[3]{};
    GrannyDeformVertices(deformer, mapping.data(), reinterpret_cast<float*>(bones.data()), 3, source, cpu);
    GrannyFreeMeshDeformer(deformer);

    IDirect3DSurface9 *target = nullptr, *readback = nullptr;
    Check(device->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr), "CreateRenderTarget");
    Check(device->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr), "CreateReadback");
    Check(device->SetRenderTarget(0,target), "SetRenderTarget");
    device->SetDepthStencilSurface(nullptr);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_AMBIENT,0x00203040);
    device->SetRenderState(D3DRS_COLORVERTEX,FALSE);
    device->SetRenderState(D3DRS_NORMALIZENORMALS,TRUE);
    device->SetRenderState(D3DRS_SPECULARENABLE,FALSE);
    device->SetRenderState(D3DRS_FOGTABLEMODE,D3DFOG_NONE);
    device->SetRenderState(D3DRS_FOGCOLOR,0x00707070);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
    device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
    device->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    IDirect3DTexture9* texture = nullptr;
    Check(device->CreateTexture(32,32,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr), "CreateTexture");
    D3DLOCKED_RECT pixels;
    Check(texture->LockRect(0,&pixels,nullptr,D3DLOCK_DISCARD), "LockTexture");
    for (unsigned y=0;y<32;++y) for(unsigned x=0;x<32;++x)
        reinterpret_cast<DWORD*>(static_cast<BYTE*>(pixels.pBits)+y*pixels.Pitch)[x] =
            0xff000080 | ((x*8)<<16) | ((y*8)<<8);
    texture->UnlockRect(0); device->SetTexture(0,texture);
    device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    D3DMATERIAL9 material{};
    material.Diffuse = {0.7f,0.8f,0.9f,0.8f}; material.Ambient = {0.3f,0.4f,0.5f,1};
    material.Emissive = {0.1f,0.05f,0.02f,1}; device->SetMaterial(&material);
    D3DLIGHT9 light{}; light.Type=D3DLIGHT_DIRECTIONAL; light.Direction={0,0,1};
    light.Diffuse={0.8f,0.7f,0.6f,1}; light.Ambient={0.02f,0.03f,0.04f,1};
    device->SetLight(0,&light); device->LightEnable(0,TRUE);
    D3DXMATRIX identity, world;
    D3DXMatrixIdentity(&identity); D3DXMatrixRotationZ(&world,0.17f);
    // Exercise inverse-transpose normal handling as well as bone skinning.
    world(0,0)*=0.9f; world(0,1)*=0.9f;
    device->SetTransform(D3DTS_WORLD,&world); device->SetTransform(D3DTS_VIEW,&identity);
    device->SetTransform(D3DTS_PROJECTION,&identity);
    bool passed = true;
    for (unsigned test=0;test<10;++test)
    {
        DrawConstants c{}; std::memset(&c,0,sizeof(c)); c.wvp=world; c.worldView=world;
        D3DXMATRIX normalMatrix; D3DXMatrixInverse(&normalMatrix,nullptr,&world);
        D3DXMatrixTranspose(&normalMatrix,&normalMatrix);
        for(unsigned col=0;col<3;++col) c.normals[col]={normalMatrix(0,col),normalMatrix(1,col),normalMatrix(2,col),0};
        c.texture[0]=c.texture[1]=identity; c.flags={1,1,test?1.0f:0.0f,0};
        light.Type=test==6?D3DLIGHT_POINT:D3DLIGHT_DIRECTIONAL;
        light.Ambient=test==6?D3DCOLORVALUE{0,0,0,1}:D3DCOLORVALUE{0.02f,0.03f,0.04f,1};
        light.Position={0,0,-0.5f}; light.Range=5;
        light.Attenuation0=1; light.Attenuation1=0.1f; light.Attenuation2=0.05f;
        device->SetLight(0,&light);
        c.initial={1,1,1,1};
        if(test)
        {
            c.initial={material.Emissive.r+material.Ambient.r*(32/255.0f+light.Ambient.r),
                material.Emissive.g+material.Ambient.g*(48/255.0f+light.Ambient.g),
                material.Emissive.b+material.Ambient.b*(64/255.0f+light.Ambient.b),material.Diffuse.a};
            c.lights[0]={0,0,-1,1}; c.lights[1]={0.56f,0.56f,0.54f,0};
            if(test==6)
            {
                c.lights[0]={0,0,-0.5f,2};
                c.lights[2]={1,0.1f,0.05f,5};
            }
        }
        device->SetRenderState(D3DRS_LIGHTING,test?TRUE:FALSE);
        device->SetRenderState(D3DRS_NORMALIZENORMALS,test==9?FALSE:TRUE);
        if(test==9) c.flags.x=0;
        device->SetRenderState(D3DRS_FOGENABLE,test==2 || test==7 || test==8?TRUE:FALSE);
        device->SetRenderState(D3DRS_RANGEFOGENABLE,TRUE);
        device->SetRenderState(D3DRS_FOGVERTEXMODE,D3DFOG_LINEAR);
        float start=0.2f,end=1.4f;
        device->SetRenderState(D3DRS_FOGSTART,*reinterpret_cast<DWORD*>(&start));
        device->SetRenderState(D3DRS_FOGEND,*reinterpret_cast<DWORD*>(&end));
        if(test==2) c.fog={start,end,0,D3DFOG_LINEAR};
        if(test==7 || test==8)
        {
            float density=0.6f;
            const DWORD mode=test==7?D3DFOG_EXP:D3DFOG_EXP2;
            device->SetRenderState(D3DRS_FOGVERTEXMODE,mode);
            device->SetRenderState(D3DRS_FOGDENSITY,*reinterpret_cast<DWORD*>(&density));
            c.fog={start,end,density,static_cast<float>(mode)};
        }
        DWORD flags=D3DTTFF_DISABLE, coordinates=0;
        if(test==3 || test==4)
        {
            c.modes.x=test==3?4.0f:3.0f;
            coordinates=test==3?D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:D3DTSS_TCI_CAMERASPACEPOSITION;
            D3DXMatrixScaling(&c.texture[0],0.3f,0.3f,1);
            c.texture[0](3,0)=c.texture[0](3,1)=0.5f;
            flags=test==3?D3DTTFF_COUNT2:(D3DTTFF_COUNT3|D3DTTFF_PROJECTED);
            if(test==4) c.modes.z=3;
        }
        if(test==5)
        {
            D3DXMatrixScaling(&c.texture[0],0.4f,0.5f,1);
            c.texture[0](2,0)=0.1f; c.texture[0](2,1)=0.2f;
            c.texture[0](3,0)=0.25f; c.texture[0](3,1)=0.3f;
            flags=D3DTTFF_COUNT2;
        }
        std::array<std::vector<DWORD>,2> images;
        for(unsigned gpu=0;gpu<2;++gpu)
        {
            device->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,coordinates);
            device->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,gpu && !(flags&D3DTTFF_PROJECTED)?0:flags);
            device->SetTransform(D3DTS_TEXTURE0,&c.texture[0]);
            device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0);
            Check(device->BeginScene(),"BeginScene");
            if(gpu)
            {
                device->SetVertexDeclaration(declaration); device->SetVertexShader(shader);
                Check(device->SetVertexShaderConstantF(0,reinterpret_cast<float*>(&c),40),"SetDrawConstants");
                Check(device->SetVertexShaderConstantF(40,reinterpret_cast<float*>(palette),216),"SetBones");
                Check(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,input,sizeof(Vertex)),"GPUDraw");
            }
            else
            {
                device->SetVertexShader(nullptr); device->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1);
                Check(device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,cpu,sizeof(cpu[0])),"CPUDraw");
            }
            device->EndScene();
            Check(device->GetRenderTargetData(target,readback),"Readback");
            Check(readback->LockRect(&pixels,nullptr,D3DLOCK_READONLY),"LockReadback");
            images[gpu].resize(128*128);
            for(unsigned y=0;y<128;++y) std::memcpy(&images[gpu][y*128],
                static_cast<BYTE*>(pixels.pBits)+y*pixels.Pitch,128*sizeof(DWORD));
            readback->UnlockRect();
        }
        unsigned bad=0, nonblack=0, maxError=0; double totalError=0;
        for(unsigned i=0;i<128*128;++i)
        {
            unsigned error=0;
            if(images[0][i]!=0xff000000) ++nonblack;
            for(unsigned channel=0;channel<4;++channel)
                error=std::max(error,static_cast<unsigned>(std::abs(
                    int((images[0][i]>>(channel*8))&255)-int((images[1][i]>>(channel*8))&255))));
            if(error>3) ++bad;
            maxError=std::max(maxError,error); totalError+=error;
        }
        const bool ok=nonblack>1000 && bad<=8 && totalError/(128*128)<0.3;
        std::printf("%s fixture=%u pixels=%u mismatches=%u max_channel_error=%u mean_pixel_error=%.5f\n",
            ok?"PASS":"FAIL",test,nonblack,bad,maxError,totalError/(128*128));
        passed &= ok;
    }
    device->SetVertexShader(nullptr); device->SetVertexDeclaration(nullptr); device->SetTexture(0,nullptr);
    texture->Release(); readback->Release(); target->Release(); declaration->Release(); shader->Release();
    device->Release(); api->Release(); DestroyWindow(window);
    return passed?0:1;
}
catch(const std::exception& error)
{
    std::printf("ERROR %s\n",error.what()); return 1;
}
