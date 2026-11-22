#version 440
#extension GL_GOOGLE_include_directive : enable
#include "lighting.glsl"
layout(location = 0) in vec3 vertexColor;
layout(location = 1) in vec2 uv;
layout(location = 2) in vec3 worldPosition;
layout(location = 3) in vec3 worldNormal;
layout(location = 4) in vec4 worldTangent;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform Object {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColor;
    vec4 eye;
    vec4 material;
    vec4 flags;
    vec4 surface; // normal scale, AO strength, normal texture present, tangent present
    vec4 emission; // linear RGB factor, strength
    vec4 transmission; // factor, IOR, model-space thickness, inverse attenuation distance
    vec4 attenuation; // linear absorption color, base-color anisotropy tap limit
    vec4 sparkle; // time, seed, enabled, reserved
};
layout(binding = 1) uniform sampler2D colorTexture;
layout(binding = 2) uniform sampler2D metallicRoughnessTexture;
layout(binding = 3) uniform sampler2D irradianceMap;
layout(binding = 4) uniform sampler2D specularMap;
layout(binding = 5) uniform sampler2D brdfMap;
layout(binding = 6) uniform sampler2D shadowMap;
layout(binding = 8) uniform sampler2D aoMap;
layout(binding = 10) uniform sampler2D normalTexture;
layout(binding = 11) uniform sampler2D occlusionTexture;
layout(binding = 12) uniform sampler2D emissiveTexture;
layout(binding = 13) uniform sampler2D opaqueScene;
layout(binding = 14) uniform sampler2D transmissionTexture;
layout(binding = 15) uniform sampler2D thicknessTexture;

vec3 transmittedLight(vec3 n,vec3 v,float roughness)
{
    float ior=transmission.y==0.0?10000.0:max(1.0,transmission.y);
    vec3 ray=refract(-v,n,1.0/ior);
    float localThickness=max(0.0,transmission.z)*texture(thicknessTexture,uv).g;
    // glTF volume thickness follows the mesh's model-space scale along the ray.
    vec3 travel=ray*localThickness*vec3(length(model[0].xyz),length(model[1].xyz),length(model[2].xyz));
    vec4 start=frame.projection*frame.view*vec4(worldPosition,1);
    vec4 end=frame.projection*frame.view*vec4(worldPosition+travel,1);
    vec2 coord=vec2(end.x/end.w*0.5+0.5,0.5-end.y/end.w*0.5);
    vec2 center=vec2(start.x/start.w*0.5+0.5,0.5-start.y/start.w*0.5);
    // Thickness zero is a thin sheet: no macroscopic displacement.
    if(localThickness<=0.0 || end.w<=0.0001) coord=center;
    vec3 light;
    if(any(lessThan(coord,vec2(0))) || any(greaterThan(coord,vec2(1)))) {
        light=frame.enabled.x>0.5?textureLod(specularMap,environmentUv(ray),roughness*frame.environment.z).rgb*frame.environment.x:
            texture(opaqueScene,textureUv(clamp(coord,vec2(0),vec2(1)))).rgb;
    } else {
        // Bounded screen-space blur for rough transmission (no scene mip-chain).
        vec2 radius=vec2(roughness*roughness*12.0*clamp(ior-1.0,0.0,1.0))/frame.viewport.xy;
        light=texture(opaqueScene,textureUv(coord)).rgb*0.2;
        for(int i=0;i<8;++i) {
            float angle=float(i)*PI/4.0;
            light+=texture(opaqueScene,textureUv(coord+vec2(cos(angle),sin(angle))*radius)).rgb*0.1;
        }
    }
    if(transmission.w>0.0 && localThickness>0.0)
        light*=pow(clamp(attenuation.rgb,vec3(0.000001),vec3(1)),vec3(length(travel)*transmission.w));
    return light;
}

vec3 mappedNormal(vec3 n)
{
    if(surface.z<0.5 || abs(surface.x)<0.000001) return n;
    vec3 t,b;
    if(surface.w>0.5) {
        t=worldTangent.xyz-n*dot(n,worldTangent.xyz);
        if(dot(t,t)<0.00000001) return n;
        t=normalize(t); b=cross(n,t)*worldTangent.w;
    } else {
        vec3 dp1=dFdx(worldPosition),dp2=dFdy(worldPosition);
        vec2 duv1=dFdx(uv),duv2=dFdy(uv);
        float det=duv1.x*duv2.y-duv1.y*duv2.x;
        if(abs(det)<0.00000001) return n;
        t=(dp1*duv2.y-dp2*duv1.y)/det;
        b=(dp2*duv1.x-dp1*duv2.x)/det;
        t-=n*dot(n,t);
        if(dot(t,t)<0.00000001) return n;
        t=normalize(t);
        b=cross(n,t)*(dot(cross(n,t),b)<0.0?-1.0:1.0);
    }
    vec3 normal=texture(normalTexture,uv).xyz*2.0-1.0;
    normal.xy*=surface.x;
    if(dot(normal,normal)<0.00000001) return n;
    return normalize(t*normal.x+b*normal.y+n*normal.z);
}

float shadowVisibility(vec3 n,vec3 l)
{
    if(frame.enabled.w<0.5) return 1.0;
    vec4 clip=frame.lightViewProjection*vec4(worldPosition,1);
    vec3 p=clip.xyz/clip.w;
    vec2 coord=p.xy*0.5+0.5;
    if(frame.viewport.w>0.5) coord.y=1.0-coord.y;
    float depth=frame.environment.w>0.5?p.z:p.z*0.5+0.5;
    if(depth<=0.0 || depth>=1.0 || any(lessThan(coord,vec2(0))) || any(greaterThan(coord,vec2(1)))) return 1.0;
    float bias=max(0.00012,0.00065*(1.0-max(dot(n,l),0.0)));
    vec2 texel=1.0/vec2(textureSize(shadowMap,0));
    float visible=0.0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        visible+=depth-bias<=texture(shadowMap,coord+vec2(x,y)*texel).r?1.0:0.0;
    return visible/9.0;
}

vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec4 sampleBaseColor()
{
    if(attenuation.w<1.5) return texture(colorTexture,uv);
    vec2 size=vec2(textureSize(colorTexture,0));
    vec2 dx=dFdx(uv)*size,dy=dFdy(uv)*size;
    // Eigenvectors of the texel-space pixel footprint preserve detail along
    // the short axis instead of choosing a blurry mip from its long axis.
    float a=dx.x*dx.x+dy.x*dy.x;
    float b=dx.x*dx.y+dy.x*dy.y;
    float c=dx.y*dx.y+dy.y*dy.y;
    float delta=sqrt(max(0.0,(a-c)*(a-c)+4.0*b*b));
    float major2=max(0.0,0.5*(a+c+delta));
    float minor=max(1.0,sqrt(max(0.0,0.5*(a+c-delta))));
    float major=sqrt(major2);
    vec2 axis=abs(b)>0.00001?normalize(vec2(b,major2-a)):(a>=c?vec2(1,0):vec2(0,1));
    int taps=int(ceil(clamp(major/minor,1.0,attenuation.w)));
    vec2 stepAxis=axis*major/size;
    vec2 gx=stepAxis/float(taps);
    vec2 gy=vec2(-axis.y,axis.x)*minor/size;
    vec4 sum=vec4(0);
    for(int i=0;i<8;++i) {
        if(i>=taps) break;
        float offset=(float(i)+0.5)/float(taps)-0.5;
        sum+=textureGrad(colorTexture,uv+stepAxis*offset,gx,gy);
    }
    return sum/float(taps);
}

float sparkleHash(vec2 p)
{
    vec3 q=fract(vec3(p.xyx)*0.1031);
    q+=dot(q,q.yzx+33.33);
    return fract((q.x+q.y)*q.z);
}

vec3 sparkleLight()
{
    // Each jittered cell has its own event duration, delay and orientation.
    // The next event changes position and brightness while fully faded out.
    vec2 grid=uv*16.0;
    vec2 cell=floor(grid);
    vec2 key=cell+vec2(sparkle.y*0.013,sparkle.y*0.037);
    float period=mix(1.4,4.7,sparkleHash(key));
    float clock=sparkle.x+sparkleHash(key+17.0)*period;
    float event=floor(clock/period);
    float local=mod(clock,period);
    vec2 eventKey=key+event*vec2(7.31,13.71);
    float duration=mix(0.45,1.1,sparkleHash(eventKey+31.0));
    float delay=sparkleHash(eventKey+47.0)*(period-duration);
    float t=clamp((local-delay)/duration,0.0,1.0);
    float envelope=pow(max(0.0,sin(t*PI)),2.0);
    vec2 center=vec2(sparkleHash(eventKey+3.0),sparkleHash(eventKey+5.0))*0.6+0.2;
    vec2 p=fract(grid)-center;
    float angle=sparkleHash(eventKey+11.0)*PI;
    p=mat2(cos(angle),-sin(angle),sin(angle),cos(angle))*p;
    float radius=mix(0.065,0.14,sparkleHash(eventKey+23.0));
    float aa=max(length(fwidth(grid))*0.5,0.008);
    float core=1.0-smoothstep(radius*0.08,radius*0.08+aa,length(p));
    float arms=exp(-min(abs(p.x),abs(p.y))/max(aa,radius*0.09))
        *(1.0-smoothstep(radius*0.35,radius,length(p)));
    float halo=exp(-dot(p,p)/(radius*radius*0.35))*0.22;
    float brightness=mix(2.5,6.0,sparkleHash(eventKey+59.0));
    return vec3(1.0,0.85,0.5)*(core+arms*0.7+halo)*envelope*brightness;
}

void main()
{
    vec4 sampleColor = sampleBaseColor();
    vec3 vertex = flags.x > 0.5 ? srgbToLinear(vertexColor) : vertexColor;
    vec3 textureColor = flags.z > 0.5 ? srgbToLinear(sampleColor.rgb) : sampleColor.rgb;
    vec3 albedo = textureColor * baseColor.rgb * vertex;
    float alpha = sampleColor.a * baseColor.a;
    if (flags.y > 0.5 && flags.y < 1.5 && alpha < material.z)
        discard;
    if (flags.y < 1.5)
        alpha = 1.0;
    vec3 result = albedo;
    if (material.w > 0.5) {
        const float pi = 3.14159265359;
        vec3 geometryNormal=normalize(worldNormal);
        vec3 n=mappedNormal(geometryNormal);
        if (!gl_FrontFacing) { geometryNormal=-geometryNormal; n=-n; }
        vec3 v = normalize(eye.xyz - worldPosition);
        vec3 l = normalize(frame.keyLight.xyz);
        vec3 h = normalize(v + l);
        vec4 mr = texture(metallicRoughnessTexture, uv);
        float metallic = clamp(material.x * mr.b, 0.0, 1.0);
        float roughness = clamp(material.y * mr.g, 0.045, 1.0);
        float ndl = max(dot(n,l),0.0), ndv = max(dot(n,v),0.001);
        float ndh = max(dot(n,h),0.0), vdh = max(dot(v,h),0.0);
        float a = roughness * roughness;
        float a2 = a * a;
        float denom = ndh * ndh * (a2 - 1.0) + 1.0;
        float distribution = a2 / max(pi * denom * denom, 0.00001);
        float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
        float geometry = (ndv / (ndv * (1.0 - k) + k))
            * (ndl / (ndl * (1.0 - k) + k));
        float dielectricF0=transmission.y==0.0?1.0:pow((transmission.y-1.0)/(transmission.y+1.0),2.0);
        vec3 f0 = mix(vec3(dielectricF0), albedo, metallic);
        vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vdh, 5.0);
        vec3 specular = distribution * geometry * fresnel / max(4.0 * ndv * ndl, 0.001);
        vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * albedo / pi;
        vec3 ambient=albedo*0.22;
        if(frame.enabled.x>0.5) {
            vec3 f=f0+(max(vec3(1.0-roughness),f0)-f0)*pow(1.0-ndv,5.0);
            vec3 indirectDiffuse=texture(irradianceMap,environmentUv(n)).rgb*albedo;
            vec3 reflection=reflect(-v,n);
            vec3 filtered=textureLod(specularMap,environmentUv(reflection),roughness*frame.environment.z).rgb;
            vec2 brdf=texture(brdfMap,vec2(ndv,roughness)).rg;
            ambient=((1.0-f)*(1.0-metallic)*indirectDiffuse+filtered*(f*brdf.x+brdf.y))*frame.environment.x;
        }
        ambient*=mix(1.0,texture(occlusionTexture,uv).r,surface.y);
        if(frame.enabled.z>0.5) {
            vec4 clip=frame.projection*frame.view*vec4(worldPosition,1);
            vec2 coord=textureUv(vec2(clip.x/clip.w*0.5+0.5,0.5-clip.y/clip.w*0.5));
            ambient*=texture(aoMap,coord).r;
        }
        result = ambient + (diffuse + specular) * ndl * frame.keyLight.w * shadowVisibility(geometryNormal,l)
            + emission.rgb*emission.w*texture(emissiveTexture,uv).rgb;
        float amount=clamp(transmission.x*texture(transmissionTexture,uv).r,0.0,1.0);
        if(amount>0.0) {
            vec3 f=f0+(1.0-f0)*pow(1.0-ndv,5.0);
            // Only the diffuse portion is replaced; surface reflections survive full transmission.
            vec3 diffuseAmbient=albedo*0.22;
            if(frame.enabled.x>0.5) {
                vec3 fi=f0+(max(vec3(1.0-roughness),f0)-f0)*pow(1.0-ndv,5.0);
                diffuseAmbient=(1.0-fi)*(1.0-metallic)*texture(irradianceMap,environmentUv(n)).rgb*albedo*frame.environment.x;
            }
            diffuseAmbient*=mix(1.0,texture(occlusionTexture,uv).r,surface.y);
            if(frame.enabled.z>0.5) {
                vec4 clip=frame.projection*frame.view*vec4(worldPosition,1);
                diffuseAmbient*=texture(aoMap,textureUv(vec2(clip.x/clip.w*0.5+0.5,0.5-clip.y/clip.w*0.5))).r;
            }
            result-=amount*(diffuseAmbient+diffuse*ndl*frame.keyLight.w*shadowVisibility(geometryNormal,l));
            result+=amount*(1.0-metallic)*(1.0-f)*albedo*transmittedLight(n,v,roughness);
        }
    }
    if(sparkle.z>0.5) result+=sparkleLight();
    // Linear output: transparent blending happens in RGBA16F, before the
    // separate presentation pass applies display sRGB encoding.
    fragColor = vec4(result, alpha);
}
