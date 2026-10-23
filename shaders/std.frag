/***************************************************************************************
Copyright (C) 2010 Maxim L. Grishin  (altmer@arts-union.ru)
This script completely free, released under the zlib license:

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

    1.The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.

    2.Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.

    3.This notice may not be removed or altered from any source distribution.
***************************************************************************************/

uniform sampler2D screenTexture;
uniform vec2 textureSize;
uniform vec2 usedTextureSize;
uniform vec2 screenSize;

void main(void)
{
    vec2 tpos = gl_TexCoord[0].xy*vec2(textureSize.x,textureSize.y)+vec2(-0.5,-0.5);

    if(tpos.x<0.0)tpos.x=0.0;
    if(tpos.y<0.0)tpos.y=0.0;

    float h = 1.0;
    float v = 1.0;

    vec2 pos = floor(tpos);

    if(h+pos.x+0.5>usedTextureSize.x)h=0.0;
    if(v+pos.y+0.5>usedTextureSize.y)v=0.0;

    tpos -= pos;

    float d1=length(tpos);
    float d2=length(tpos-vec2(h,0.0));
    float d3=length(tpos-vec2(h,v));
    float d4=length(tpos-vec2(0.0,v));

    if(d1>=1.0)d1=0.0;
    else d1=(1.0-d1);

    if(d2>=1.0)d2=0.0;
    else d2=(1.0-d2);

    if(d3>=1.0)d3=0.0;
    else d3=(1.0-d3);

    if(d4>=1.0)d4=0.0;
    else d4=(1.0-d4);

    pos = pos/textureSize;
    h=(h+0.5)/textureSize.x;
    v=(v+0.5)/textureSize.y;
    float hpx=0.5/textureSize.x;
    float hpy=0.5/textureSize.y;

    vec3 lu=texture2D(screenTexture, pos+vec2(hpx,hpy)).xyz;
    vec3 ru=texture2D(screenTexture, pos+vec2(h,hpy)).xyz;
    vec3 rd=texture2D(screenTexture, pos+vec2(h,v)).xyz;
    vec3 ld=texture2D(screenTexture, pos+vec2(hpx,v)).xyz;

    float llu=length(lu-rd);
    float lru=length(ru-ld);

    float hlu=length(lu-ru);
    float hld=length(rd-ld);

    float vlu=length(lu-ru);
    float vru=length(rd-ld);

    float lu_dist=tpos.x+tpos.y;
    float ru_dist=1.0-tpos.x+tpos.y;

    vec2 diag;

    if( ( (llu<0.2 && (hlu<0.2 || vlu<0.2 || lru>0.3)) && ru_dist>=0.6 && ru_dist<1.4) ||
          ((lru<0.2 && (hld<0.2 || vru<0.2 || llu>0.3)) && lu_dist>=0.6 && lu_dist<1.4)  )
    {
        diag = normalize( vec2(llu+0.1,lru+0.1) );
        diag = vec2(1.1,1.1)-diag;

        d1*=diag.x;
        d2*=diag.y;
        d3*=diag.x;
        d4*=diag.y;
    }

    gl_FragColor.xyz = (lu*d1+ru*d2+rd*d3+ld*d4)/(d1+d2+d3+d4);
}
