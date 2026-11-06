// LiteStrike - tiny online FPS. Win32 + OpenGL 1.x + UDP. No external libs.
// Server:  game.exe server        Client:  game.exe <server-ip>   (default 127.0.0.1)
#include <winsock2.h>
#include <windows.h>
#include <GL/gl.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#pragma comment(lib,"ws2_32.lib")
#pragma comment(lib,"opengl32.lib")
#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")

#define PORT 27015
#define MAXP 16
#pragma pack(push,1)
struct InPkt { float x,y,z,yaw,pitch; unsigned char shots, ack; };
struct PState { float x,y,z,yaw,pitch; short hp, score; unsigned char id, spawn; };
struct Snap { unsigned char me, n; PState p[MAXP]; };
#pragma pack(pop)

// ---- shared map: axis-aligned walls/crates (x0,z0,x1,z1,height) ----
struct Box { float x0,z0,x1,z1,h; };
static Box walls[] = {
  {-20,-20,20,-19.5f,4},{-20,19.5f,20,20,4},{-20,-20,-19.5f,20,4},{19.5f,-20,20,20,4},
  {-4,-4,-1,-1,2.5f},{-8,6,-5,9,2},{6,-8,10,-6,3},{-2,5,2,6,3.5f},
  {8,8,12,12,2},{-12,-12,-9,-9,3},{0,-12,1,-7,3}
};
static const int NW = sizeof(walls)/sizeof(walls[0]);
static bool hitWall(float x, float z) {
  for (int i=0;i<NW;i++) { const Box&w=walls[i];
    if (x>w.x0-.4f&&x<w.x1+.4f&&z>w.z0-.4f&&z<w.z1+.4f) return true; }
  return false;
}

// ================= SERVER =================
static float raySphere(const float*o,const float*d,float cx,float cy,float cz,float r){
  float ox=o[0]-cx,oy=o[1]-cy,oz=o[2]-cz;
  float b=ox*d[0]+oy*d[1]+oz*d[2], c=ox*ox+oy*oy+oz*oz-r*r, disc=b*b-c;
  if (disc<0) return -1; float t=-b-sqrtf(disc); return t>0?t:-1;
}
static float rayBox(const float*o,const float*d,const Box&b){
  float lo[3]={b.x0,0,b.z0}, hi[3]={b.x1,b.h,b.z1}, tn=0, tf=1e9f;
  for (int i=0;i<3;i++) {
    if (fabsf(d[i])<1e-6f) { if (o[i]<lo[i]||o[i]>hi[i]) return -1; continue; }
    float t1=(lo[i]-o[i])/d[i], t2=(hi[i]-o[i])/d[i];
    if (t1>t2){float s=t1;t1=t2;t2=s;}
    if (t1>tn) tn=t1; if (t2<tf) tf=t2; if (tn>tf) return -1;
  }
  return tn;
}
static void respawn(PState&s){
  do { s.x=(rand()%360-180)/10.f; s.z=(rand()%360-180)/10.f; } while (hitWall(s.x,s.z));
  s.y=0; s.hp=100; s.spawn++;
}
struct SP { sockaddr_in addr; PState st; unsigned char seq; DWORD last; bool used; };
static SP pl[MAXP];

static void fire(int i){
  PState&a=pl[i].st; float cp=cosf(a.pitch);
  float o[3]={a.x,a.y+1.6f,a.z}, d[3]={cp*sinf(a.yaw),sinf(a.pitch),-cp*cosf(a.yaw)};
  float ht=1e9f;
  for (int k=0;k<NW;k++){ float t=rayBox(o,d,walls[k]); if (t>=0&&t<ht) ht=t; }
  int hit=-1, dmg=0;
  for (int j=0;j<MAXP;j++){ if (j==i||!pl[j].used) continue; PState&b=pl[j].st;
    float th=raySphere(o,d,b.x,b.y+1.65f,b.z,.22f), tb=raySphere(o,d,b.x,b.y+1.f,b.z,.5f);
    if (th>=0&&th<ht){ht=th;hit=j;dmg=80;} else if (tb>=0&&tb<ht){ht=tb;hit=j;dmg=25;} }
  if (hit>=0){ pl[hit].st.hp-=dmg;
    if (pl[hit].st.hp<=0){ a.score++; respawn(pl[hit].st); } }
}
static void runServer(){
  SOCKET s=socket(AF_INET,SOCK_DGRAM,0); sockaddr_in a={}; a.sin_family=AF_INET;
  a.sin_port=htons(PORT); a.sin_addr.s_addr=INADDR_ANY;
  if (bind(s,(sockaddr*)&a,sizeof a)<0){ printf("bind failed\n"); return; }
  u_long nb=1; ioctlsocket(s,FIONBIO,&nb);
  printf("Server running on UDP %d\n",PORT);
  DWORD lastSend=0;
  for(;;){
    InPkt in; sockaddr_in from; int fl=sizeof from;
    while (recvfrom(s,(char*)&in,sizeof in,0,(sockaddr*)&from,&fl)==sizeof in){
      fl=sizeof from; int id=-1;
      for (int i=0;i<MAXP;i++) if (pl[i].used&&pl[i].addr.sin_addr.s_addr==from.sin_addr.s_addr&&pl[i].addr.sin_port==from.sin_port) id=i;
      if (id<0){ for (int i=0;i<MAXP;i++) if(!pl[i].used){id=i;break;}
        if (id<0) continue;
        memset(&pl[id],0,sizeof(SP)); pl[id].used=true; pl[id].addr=from; pl[id].st.id=(unsigned char)id;
        pl[id].seq=in.shots; respawn(pl[id].st); printf("player %d joined\n",id); }
      SP&p=pl[id]; p.last=GetTickCount();
      if (in.ack==p.st.spawn){ p.st.x=in.x;p.st.y=in.y;p.st.z=in.z;p.st.yaw=in.yaw;p.st.pitch=in.pitch; }
      if (in.shots!=p.seq){ p.seq=in.shots; if (in.ack==p.st.spawn) fire(id); }
    }
    DWORD now=GetTickCount();
    for (int i=0;i<MAXP;i++) if (pl[i].used&&now-pl[i].last>5000){ pl[i].used=false; printf("player %d left\n",i); }
    if (now-lastSend>=33){ lastSend=now;
      Snap sn; memset(&sn,0,sizeof sn); int n=0;
      for (int i=0;i<MAXP;i++) if (pl[i].used) sn.p[n++]=pl[i].st; sn.n=(unsigned char)n;
      for (int i=0;i<MAXP;i++) if (pl[i].used){ sn.me=(unsigned char)i;
        sendto(s,(char*)&sn,sizeof sn,0,(sockaddr*)&pl[i].addr,sizeof(sockaddr_in)); } }
    Sleep(1);
  }
}

// ================= CLIENT =================
static bool running=true;
static LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l){
  if (m==WM_CLOSE||m==WM_DESTROY){ running=false; return 0; }
  return DefWindowProcA(h,m,w,l);
}
static void box(float x0,float y0,float z0,float x1,float y1,float z1,float r,float g,float b){
  glBegin(GL_QUADS);
  glColor3f(r,g,b); glVertex3f(x0,y1,z0);glVertex3f(x1,y1,z0);glVertex3f(x1,y1,z1);glVertex3f(x0,y1,z1);
  glColor3f(r*.5f,g*.5f,b*.5f); glVertex3f(x0,y0,z0);glVertex3f(x0,y0,z1);glVertex3f(x1,y0,z1);glVertex3f(x1,y0,z0);
  glColor3f(r*.8f,g*.8f,b*.8f); glVertex3f(x0,y0,z1);glVertex3f(x1,y0,z1);glVertex3f(x1,y1,z1);glVertex3f(x0,y1,z1);
  glVertex3f(x0,y0,z0);glVertex3f(x0,y1,z0);glVertex3f(x1,y1,z0);glVertex3f(x1,y0,z0);
  glColor3f(r*.65f,g*.65f,b*.65f); glVertex3f(x0,y0,z0);glVertex3f(x0,y0,z1);glVertex3f(x0,y1,z1);glVertex3f(x0,y1,z0);
  glVertex3f(x1,y0,z0);glVertex3f(x1,y1,z0);glVertex3f(x1,y1,z1);glVertex3f(x1,y0,z1);
  glEnd();
}
static const float PAL[8][3]={{.9f,.2f,.3f},{.2f,.6f,.95f},{.3f,.85f,.4f},{.95f,.75f,.2f},{.7f,.3f,.9f},{.2f,.9f,.85f},{.95f,.5f,.2f},{.9f,.9f,.9f}};
static void agent(const PState&p){ // low-poly "agent" built from boxes
  const float*c=PAL[p.id&7];
  glPushMatrix(); glTranslatef(p.x,p.y,p.z); glRotatef(-p.yaw*57.2958f,0,1,0);
  box(-.28f,0,-.14f,-.04f,.8f,.14f,.15f,.15f,.2f); box(.04f,0,-.14f,.28f,.8f,.14f,.15f,.15f,.2f); // legs
  box(-.32f,.8f,-.2f,.32f,1.4f,.2f,c[0],c[1],c[2]);                                              // torso
  box(-.46f,.85f,-.12f,-.32f,1.35f,.12f,c[0]*.7f,c[1]*.7f,c[2]*.7f);                           // arms
  box(.32f,.85f,-.12f,.46f,1.35f,.12f,c[0]*.7f,c[1]*.7f,c[2]*.7f);
  box(-.18f,1.43f,-.18f,.18f,1.8f,.18f,.85f,.7f,.6f);                                            // head
  box(-.14f,1.58f,-.2f,.14f,1.68f,-.17f,c[0],c[1],c[2]);                                         // visor
  box(.1f,1.0f,-.75f,.2f,1.12f,-.1f,.1f,.1f,.1f);                                                // gun
  glPopMatrix();
}
int main(int argc,char**argv){
  WSADATA wd; WSAStartup(MAKEWORD(2,2),&wd);
  if (argc>1&&!strcmp(argv[1],"server")){ runServer(); return 0; }
  const char*ip=argc>1?argv[1]:"127.0.0.1";
  SOCKET s=socket(AF_INET,SOCK_DGRAM,0); sockaddr_in sa={}; sa.sin_family=AF_INET;
  sa.sin_port=htons(PORT); sa.sin_addr.s_addr=inet_addr(ip);
  connect(s,(sockaddr*)&sa,sizeof sa); u_long nb=1; ioctlsocket(s,FIONBIO,&nb);

  WNDCLASSA wc={}; wc.lpfnWndProc=WndProc; wc.hInstance=GetModuleHandle(0); wc.lpszClassName="LS";
  wc.hCursor=LoadCursor(0,IDC_ARROW); RegisterClassA(&wc);
  HWND hw=CreateWindowA("LS","LiteStrike",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,1280,720,0,0,wc.hInstance,0);
  HDC dc=GetDC(hw); PIXELFORMATDESCRIPTOR pf={}; pf.nSize=sizeof pf; pf.nVersion=1;
  pf.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER; pf.cColorBits=32; pf.cDepthBits=24;
  SetPixelFormat(dc,ChoosePixelFormat(dc,&pf),&pf);
  HGLRC rc=wglCreateContext(dc); wglMakeCurrent(dc,rc);
  typedef BOOL(WINAPI*SwapFn)(int); SwapFn swap=(SwapFn)wglGetProcAddress("wglSwapIntervalEXT"); if (swap) swap(1);
  glEnable(GL_DEPTH_TEST); ShowCursor(FALSE);

  float x=0,y=0,z=0,vy=0,yaw=0,pitch=0; unsigned char shots=0, ack=0; short hp=100, score=0;
  Snap snap; memset(&snap,0,sizeof snap); float cool=0, tracer=0, netT=0, titleT=0;
  LARGE_INTEGER fq,t0; QueryPerformanceFrequency(&fq); QueryPerformanceCounter(&t0);
  while (running){
    MSG m; while (PeekMessageA(&m,0,0,0,PM_REMOVE)){ TranslateMessage(&m); DispatchMessageA(&m); }
    LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
    float dt=(float)(t1.QuadPart-t0.QuadPart)/fq.QuadPart; t0=t1; if (dt>.05f) dt=.05f;
    RECT cr; GetClientRect(hw,&cr); int W=cr.right,H=cr.bottom>0?cr.bottom:1; float asp=(float)W/H;
    bool focus=GetForegroundWindow()==hw;
    if (focus){ POINT c={W/2,H/2}, p; ClientToScreen(hw,&c); GetCursorPos(&p);
      yaw+=(p.x-c.x)*.0025f; pitch-=(p.y-c.y)*.0025f; SetCursorPos(c.x,c.y);
      if (pitch>1.5f) pitch=1.5f; if (pitch<-1.5f) pitch=-1.5f;
      if (GetAsyncKeyState(VK_ESCAPE)&0x8000) running=false; }
    // movement
    float fx=sinf(yaw),fz=-cosf(yaw),rx=cosf(yaw),rz=sinf(yaw),mx=0,mz=0;
    if (focus){ if(GetAsyncKeyState('W')&0x8000){mx+=fx;mz+=fz;} if(GetAsyncKeyState('S')&0x8000){mx-=fx;mz-=fz;}
      if(GetAsyncKeyState('D')&0x8000){mx+=rx;mz+=rz;} if(GetAsyncKeyState('A')&0x8000){mx-=rx;mz-=rz;} }
    float ml=sqrtf(mx*mx+mz*mz); if (ml>0){mx/=ml;mz/=ml;}
    float nx=x+mx*6*dt, nz=z+mz*6*dt;
    if (!hitWall(nx,z)) x=nx; if (!hitWall(x,nz)) z=nz;
    if (focus&&(GetAsyncKeyState(VK_SPACE)&0x8000)&&y<=0.001f) vy=7;
    vy-=20*dt; y+=vy*dt; if (y<0){y=0;vy=0;}
    cool-=dt; tracer-=dt;
    if (focus&&(GetAsyncKeyState(VK_LBUTTON)&0x8000)&&cool<=0&&hp>0){ shots++; cool=.12f; tracer=.05f; }
    // network
    netT+=dt; if (netT>=1.f/60){ netT=0; InPkt in={x,y,z,yaw,pitch,shots,ack}; send(s,(char*)&in,sizeof in,0); }
    Snap sn; while (recv(s,(char*)&sn,sizeof sn,0)==sizeof sn) snap=sn;
    for (int i=0;i<snap.n;i++) if (snap.p[i].id==snap.me){ const PState&me=snap.p[i]; hp=me.hp; score=me.score;
      if (me.spawn!=ack){ ack=me.spawn; x=me.x; y=me.y; z=me.z; vy=0; } }
    // render
    glViewport(0,0,W,H); glClearColor(.45f,.65f,.9f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); float top=.1f*tanf(37.5f*3.14159f/180), rt=top*asp; glFrustum(-rt,rt,-top,top,.1,200);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glRotatef(-pitch*57.2958f,1,0,0); glRotatef(yaw*57.2958f,0,1,0); glTranslatef(-x,-(y+1.6f),-z);
    box(-20,-.1f,-20,20,0,20,.28f,.3f,.34f);
    for (int i=0;i<NW;i++){ const Box&w=walls[i]; float c=.45f+.05f*(i%5); box(w.x0,0,w.z0,w.x1,w.h,w.z1,c,c*.9f,c*.8f); }
    for (int i=0;i<snap.n;i++) if (snap.p[i].id!=snap.me) agent(snap.p[i]);
    if (tracer>0){ glBegin(GL_LINES); glColor3f(1,.9f,.3f); float cp=cosf(pitch);
      glVertex3f(x+rx*.2f,y+1.4f,z+rz*.2f); glVertex3f(x+cp*sinf(yaw)*60,y+1.6f+sinf(pitch)*60,z-cp*cosf(yaw)*60); glEnd(); }
    // viewmodel + HUD (view space)
    glClear(GL_DEPTH_BUFFER_BIT); glLoadIdentity();
    box(.1f,-.2f,-.7f,.18f,-.1f,-.25f,.12f,.12f,.12f);
    glDisable(GL_DEPTH_TEST); glBegin(GL_LINES); glColor3f(0,1,0);
    glVertex3f(-.03f,0,-1);glVertex3f(-.01f,0,-1);glVertex3f(.01f,0,-1);glVertex3f(.03f,0,-1);
    glVertex3f(0,-.03f,-1);glVertex3f(0,-.01f,-1);glVertex3f(0,.01f,-1);glVertex3f(0,.03f,-1); glEnd();
    float hpw=.6f*(hp>0?hp:0)/100.f; box(-.3f,-.72f,-1,-.3f+hpw,-.69f,-1,.2f,.9f,.3f);
    glEnable(GL_DEPTH_TEST);
    SwapBuffers(dc);
    titleT+=dt; if (titleT>.25f){ titleT=0; char t[96]; sprintf(t,"LiteStrike | HP %d | Kills %d | Players %d | WASD move, Space jump, LMB shoot, Esc quit",hp,score,snap.n); SetWindowTextA(hw,t); }
  }
  return 0;
}
