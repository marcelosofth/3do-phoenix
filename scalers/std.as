/***************************************************************************************
Copyright (C) 2016 Maxim L. Grishin  (altmer@arts-union.ru)
This script completely free, released under the zlib license:

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

    1.The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.

    2.Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.

    3.This notice may not be removed or altered from any source distribution.
***************************************************************************************/

const int IMF_LINE45 = 0x00001;
const int IMF_LINE26 = 0x00002;
const int IMF_LINE70 = 0x00004;
const int IMF_LINEXX = 0x00008;
int DELTA;

int topo_i,topo_j,topo_type;
int w;
int h;
int factor;
array<uint> edges;

bool Transp(uint color)
{
    return (color&0x80000000)!=0;
}

int Domen(uint color)
{
	if(Transp(color))return 0x80000000;
    return color&0x8000;
}

int Red(uint color)
{
    return (color&31)<<3;
}

int Green(uint color)
{
    return ((color>>5)&31)<<3;
}

int Blue(uint color)
{
    return ((color>>10)&31)<<3;
}

int AMVRed(uint color)
{
    return ((color>>16)&7)<<5;
}

int AMVGreen(uint color)
{
    return ((color>>19)&7)<<5;
}

int AMVBlue(uint color)
{
    return ((color>>22)&7)<<5;
}

uint makeAMVColor(uint patt, int r, int g, int b, int ar, int ag, int ab)
{
    return  (patt&0x80008000)|(r>>3)|((g>>3)<<5)|((b>>3)<<10)
            |((ar>>5)<<16)|((ag>>5)<<19)|((ab>>5)<<22);
}

uint get_color(int i, int j)
{
    int t_i=i,t_j=j;
    switch(topo_type)
    {
    case 1:
        t_i=(j-topo_j)+topo_i;
        t_j=topo_j-(i-topo_i);
        break;
    case 2:
        t_i=topo_i-(i-topo_i);
        t_j=topo_j-(j-topo_j);
        break;
    case 3:
        t_i=topo_i-(j-topo_j);
        t_j=(i-topo_i)+topo_j;
        break;
    };
    i=t_i;
    j=t_j;
	
    return src(j,i);
}

void fill_side(int ii, int jj, float x0, float y1)
{

	uint cy=get_color(ii-1,jj);
	uint cx=get_color(ii,jj+1);
	
	if(Transp(cx))cx=cy;
    if(Transp(cy))cy=cx;
	
	corner(x0+0.5,0.5-y1,jj,ii,topo_type,cx,cy);	
}

bool equal(uint c1, uint c2)
{
	if(Domen(c1)!=Domen(c2))return false;
	int total=dist(c1,c2);
	if(total>DELTA)return false;
	return true;
}

int make_patterns(int i, int j)
{
    int rv=0;

    uint32 curr=get_color(i,j);
    uint32 left=get_color(i,j-1);
    uint32 right=get_color(i,j+1);
    uint32 up=get_color(i-1,j);
    uint32 down=get_color(i+1,j);

    if((equal(curr,left) && equal(curr,right)) || (equal(curr,up) && equal(curr,down)))return rv;//IMF_LINEXX;
	

    uint32 left_up=get_color(i-1,j-1);
    uint32 left_down=get_color(i+1,j-1);
    uint32 right_up=get_color(i-1,j+1);
    uint32 right_down=get_color(i+1,j+1);

    uint32 right_right_down=get_color(i+1,j+2);
    uint32 up_up=get_color(i-2,j);
    uint32 left_up_up=get_color(i-2,j-1);
    uint32 right_right=get_color(i,j+2);

    if((equal(right_up,right) || Transp(curr) || (Transp(right_up) && !equal(right_up,curr))) && equal(right,up) && !equal(curr,up))
    {
        if(equal(up_up,up) && !equal(left_up,up) && !equal(left_up_up,up) && equal(right_down,up))
        {
            rv|=IMF_LINE26;
            fill_side(i,j,0.0,-0.5);
        }
        if(equal(right_right,up) && !equal(left_down,up) && !equal(right_right_down,up) && equal(left_up,up))
        {
            rv|=IMF_LINE70;
            fill_side(i,j,-0.5,0.0);
        }
        if((rv&(IMF_LINE26|IMF_LINE70))==0)
        {
            rv|=IMF_LINE45;
            fill_side(i,j,0.0,0.0);
        }
    }
    if(equal(left,curr) && equal(down,curr) && !equal(curr,up) && !equal(curr,right) && (!equal(curr,right_up) || Transp(curr)))
    {
        if(equal(right_down,curr) && (equal(up,right) || !Transp(right)))
        {
            if(equal(right_right_down,curr) && !equal(left_up,curr))
            {
                rv|=IMF_LINE70;
                fill_side(i,j,-0.5,0.0);
            }
            else
            {
                rv|=IMF_LINE45;
                fill_side(i,j,0.0,0.0);
            }
        }
        if(equal(left_up,curr) && (equal(up,right) || !Transp(up)))
        {
            if(equal(left_up_up,curr) && !equal(right_down,curr) && equal(right,right_down))
            {
                rv|=IMF_LINE26;
                fill_side(i,j,0.0,-0.5);
            }
            else
            {
                rv|=IMF_LINE45;
                fill_side(i,j,0.0,0.0);
            }
        }
    }

    if(rv==0 && !Transp(curr) && (equal(left,curr) || equal(right_right_down,curr)) && equal(right_down,curr) &&
            !equal(left_up,curr) && !equal(right,curr) && !equal(right_right,curr) && !equal(up,curr)
            && Transp(right)==Transp(up))
    {
        if(equal(left_up,up))
        {
            rv|=IMF_LINE70;
            fill_side(i,j,-0.5,0.0);
        }
        else
        {
            rv|=IMF_LINE45;
            fill_side(i,j,0.0,0.0);
        }
    }

    if(rv==0 && Transp(curr) && equal(curr,down) && !equal(right,curr) && !equal(up,curr) && !equal(left,curr))
    {
        rv|=IMF_LINE45;
        fill_side(i,j,0.0,0.0);
    }

    if(rv==0 && Transp(curr) && equal(curr,left) && !equal(right,curr) && !equal(up,curr) && !equal(down,curr))
    {
        rv|=IMF_LINE45;
        fill_side(i,j,0.0,0.0);
    }

    if(rv==0 && !Transp(curr) && equal(curr,left_up) && equal(curr,right_down) && !equal(right,curr) && !equal(up,curr) && Transp(right)==Transp(up))
    {
        rv|=IMF_LINE45;
        fill_side(i,j,0.0,0.0);
    }

    return rv;
}
bool isClear(uint val)
{
	if(val==0)return true;
	return false;
}

bool isClear(uint val, int corner)
{	
	if(corner==0 && (val&0xff)==0 && ((val>>8)&IMF_LINE70)==0 && ((val>>24)&IMF_LINE26)==0)return true;
	if(corner==1 && ((val>>8)&0xff)==0 && ((val>>16)&IMF_LINE70)==0 && (val&IMF_LINE26)==0)return true;
	if(corner==2 && ((val>>16)&0xff)==0 && ((val>>24)&IMF_LINE70)==0 && ((val>>8)&IMF_LINE26)==0)return true;
	if(corner==3 && ((val>>24)&0xff)==0 && (val&IMF_LINE70)==0 && ((val>>16)&IMF_LINE26)==0)return true;
	return false;
}

uint get_edge(int i, int j)
{
    uint rv;
    if(i<0 || i>=h || j<0 || j>=w)
    {
        rv=0;
    }
    else
    {
        rv=edges[i*w+j];
    }
    return rv;
}

void make_filtration(int ii, int jj)
{
    uint curr=get_edge(ii,jj);
    uint right_up=get_edge(ii-1,jj+1);
    uint right=get_edge(ii,jj+1);
    uint up=get_edge(ii-1,jj);

    uint c_col=get_color(ii,jj);
    uint ru_col=get_color(ii-1,jj+1);
    uint r_col=get_color(ii,jj+1);
    uint u_col=get_color(ii-1,jj);

    uint right_down=get_edge(ii+1,jj+1);
    uint down=get_edge(ii+1,jj);

    uint rd_col=get_color(ii+1,jj+1);
    uint d_col=get_color(ii+1,jj);

    uint left_down=get_edge(ii+1,jj-1);
    uint left=get_edge(ii,jj-1);

    uint ld_col=get_color(ii+1,jj-1);
    uint l_col=get_color(ii,jj-1);

    uint left_up=get_edge(ii-1,jj-1);

    uint lu_col=get_color(ii-1,jj-1);

        if(!(Transp(c_col) || Transp(r_col) || Transp(u_col))
            && isClear(curr,0) && isClear(up,1) && isClear(right,3)
            && equal(c_col,r_col) && equal(r_col,u_col))
    {
		if(!isClear(right_up,2) || Transp(ru_col) || !equal(c_col,ru_col))ru_col=mix(0.5,u_col,r_col);
		
        for(int i=0;i<factor/2;i++)
        {
            for(int j=factor/2;j<factor;j++)
            {
                float x=(j-factor/2)/(factor*1.0);
                float y=(factor/2-i)/(factor*1.0);
								
                set(jj*factor+j,ii*factor+i,mix(x,y,u_col,ru_col,r_col,c_col));
            }
        }
    }


    if(!(Transp(c_col) || Transp(r_col) || Transp(d_col))
            && isClear(curr,1) && isClear(right,2) && isClear(down,0) 
            && equal(c_col,r_col) && equal(r_col,d_col))
    {
		if(!isClear(right_down,3) || Transp(rd_col) || !equal(c_col,rd_col))rd_col=mix(0.5,d_col,r_col);
		
        for(int i=factor/2;i<factor;i++)
        {
            for(int j=factor/2;j<factor;j++)
            {
                float x=(j-factor/2)/(factor*1.0);
                float y=(factor/2+factor-i-1)/(factor*1.0);

                set(jj*factor+j,ii*factor+i,mix(x,y,c_col,r_col,rd_col,d_col));
            }
        }
    }


    if(!(Transp(c_col) || Transp(d_col) || Transp(l_col))
            && isClear(curr,2) && isClear(down,3) && isClear(left,1)
            && equal(c_col,d_col) && equal(d_col,l_col))
    {
		if(!isClear(left_down,0) || Transp(ld_col) || !equal(c_col,ld_col))ld_col=mix(0.5,d_col,l_col);
		
        for(int i=factor/2;i<factor;i++)
        {
            for(int j=0;j<factor/2;j++)
            {
                float x=(j+factor/2)/(factor*1.0);
                float y=(factor/2+factor-i-1)/(factor*1.0);

                set(jj*factor+j,ii*factor+i,mix(x,y,l_col,c_col,d_col,ld_col));
            }
        }
    }

    if(!(Transp(c_col) || Transp(l_col) || Transp(u_col))
            && isClear(curr,3) && isClear(up,2) && isClear(left,0)
            && equal(c_col,l_col) && equal(l_col,u_col))
    {
		if(!isClear(left_up,1) || Transp(lu_col) || !equal(c_col,lu_col))lu_col=mix(0.5,u_col,l_col);
        for(int i=0;i<factor/2;i++)
        {
            for(int j=0;j<factor/2;j++)
            {
                float x=(j+factor/2)/(factor*1.0);
                float y=(factor/2-i)/(factor*1.0);

                set(jj*factor+j,ii*factor+i,mix(x,y,lu_col,u_col,c_col,l_col));
            }
        }
    }

}



//точка входа в скрипт, она должна быть именно такой и никакой другой!
void filter(int scale, uint ws, uint hs)
{    
	w=ws;
	h=hs;
	factor=scale;
	
	topo_type = 0;
    	
	if(w==1 || h==1) return;
	
	edges.resize(w*h);
	for(int i=0;i<h*w;i++)edges[i]=0;
	
	DELTA=0;
	for(int i=0;i<h;i++)
    {
        for(int j=0;j<w;j++)
        {
			uint curr=get_edge(i,j);
            topo_i=i;
            topo_j=j;
            topo_type=0;
            if(isClear(get_edge(i-1,j),1) && isClear(get_edge(i,j+1),3)) edges[i*w+j]|=make_patterns(i,j); //ru
            topo_type=1;
            if(isClear(get_edge(i,j+1),2) && isClear(get_edge(i+1,j),0)) edges[i*w+j]|=make_patterns(i,j)<<8; //rd
            topo_type=2;
            if(isClear(get_edge(i+1,j),3) && isClear(get_edge(i,j-1),1)) edges[i*w+j]|=make_patterns(i,j)<<16; //ld
            topo_type=3;
            if(isClear(get_edge(i,j-1),0) && isClear(get_edge(i-1,j),2)) edges[i*w+j]|=make_patterns(i,j)<<24; //lu
        }
    }
	
		
	DELTA=32;
	for(int i=0;i<h;i++)
    {
        for(int j=0;j<w;j++)
        {
			uint curr=get_edge(i,j);
            topo_i=i;
            topo_j=j;
            topo_type=0;
            if(isClear(get_edge(i-1,j),1) && isClear(get_edge(i,j+1),3)) edges[i*w+j]|=make_patterns(i,j); //ru
            topo_type=1;
            if(isClear(get_edge(i,j+1),2) && isClear(get_edge(i+1,j),0)) edges[i*w+j]|=make_patterns(i,j)<<8; //rd
            topo_type=2;
            if(isClear(get_edge(i+1,j),3) && isClear(get_edge(i,j-1),1)) edges[i*w+j]|=make_patterns(i,j)<<16; //ld
            topo_type=3;
            if(isClear(get_edge(i,j-1),0) && isClear(get_edge(i-1,j),2)) edges[i*w+j]|=make_patterns(i,j)<<24; //lu
        }
    }
	
	
	DELTA=64;
	for(int i=0;i<h;i++)
    {
        for(int j=0;j<w;j++)
        {
			uint curr=get_edge(i,j);
            topo_i=i;
            topo_j=j;
            topo_type=0;
            if(isClear(get_edge(i-1,j),1) && isClear(get_edge(i,j+1),3)) edges[i*w+j]|=make_patterns(i,j); //ru
            topo_type=1;
            if(isClear(get_edge(i,j+1),2) && isClear(get_edge(i+1,j),0)) edges[i*w+j]|=make_patterns(i,j)<<8; //rd
            topo_type=2;
            if(isClear(get_edge(i+1,j),3) && isClear(get_edge(i,j-1),1)) edges[i*w+j]|=make_patterns(i,j)<<16; //ld
            topo_type=3;
            if(isClear(get_edge(i,j-1),0) && isClear(get_edge(i-1,j),2)) edges[i*w+j]|=make_patterns(i,j)<<24; //lu
        }
    }
	
	DELTA=96;
	for(int i=0;i<h;i++)
    {
        for(int j=0;j<w;j++)
        {
			uint curr=get_edge(i,j);
            topo_i=i;
            topo_j=j;
            topo_type=0;
            if(isClear(get_edge(i-1,j),1) && isClear(get_edge(i,j+1),3)) edges[i*w+j]|=make_patterns(i,j); //ru
            topo_type=1;
            if(isClear(get_edge(i,j+1),2) && isClear(get_edge(i+1,j),0)) edges[i*w+j]|=make_patterns(i,j)<<8; //rd
            topo_type=2;
            if(isClear(get_edge(i+1,j),3) && isClear(get_edge(i,j-1),1)) edges[i*w+j]|=make_patterns(i,j)<<16; //ld
            topo_type=3;
            if(isClear(get_edge(i,j-1),0) && isClear(get_edge(i-1,j),2)) edges[i*w+j]|=make_patterns(i,j)<<24; //lu
        }
    }
	
	DELTA=128;
	for(int i=0;i<h;i++)
    {
        for(int j=0;j<w;j++)
        {
            topo_i=i;
            topo_j=j;
            topo_type=0;
            make_filtration(i,j);
        }
    }
	
}










