# #1493's CONTROL: the needle, planted, and proved to be there before any absence means
# anything (#708's rule, one domain over).
#
#   python3 i1493_control.py | python3 i1493_poses.py /dev/stdin
#
# i1493_poses.py recovers a camera pose from a board-plane homography under an assumed
# pinhole model and intersects rays from it. That arithmetic can be wrong in ways that read
# as a finding -- a mirrored pose, a transposed rotation, a focal length off by the
# principal point -- and every one of them would come back as "the rays do not meet", which
# is exactly the sentence the rig's real footage might produce for real reasons. So the
# analysis is first run against a rig that was BUILT from known poses:
#
#   three cameras, 450 mm from the board centre, 35 degrees above its plane, at azimuths
#   90, 210 and 330 degrees, f = 537 px (a 1280-wide 100-degree lens, the README's hardware
#   reference), principal point at the frame centre, no distortion at all.
#
# H is then K [r1 r2 t] by construction, which is what planeOf() would return of such a
# camera, and two points are projected into all three:
#
#   dart 1  ON the board plane          -> residual 0.0 mm, height  +0.0 mm
#   dart 2  20 mm PROUD of the plane    -> residual 0.0 mm, height +20.0 mm, and the two
#           cameras' unprojections 53.6 mm apart IN the plane
#
# The second row is the whole of triangulation's claim over unprojection in one line: the
# rays meet exactly, 20 mm off the board, where mapping either tip ONTO the board would
# have produced two confident positions half a sector apart and no way to tell.
#
# A CONTROL AND NOT A FIXTURE: nothing here touches the rig footage, and no number in it is
# a threshold.
import math, sys

MM=170.0
f=537.0; cx=640.0; cy=360.0
K=[[f,0,cx],[0,f,cy],[0,0,1]]

def matvec(M,v): return [sum(M[r][c]*v[c] for c in range(3)) for r in range(3)]
def mm(A,B): return [[sum(A[r][k]*B[k][c] for k in range(3)) for c in range(3)] for r in range(3)]
def rotx(a):
    c,s=math.cos(a),math.sin(a); return [[1,0,0],[0,c,-s],[0,s,c]]
def rotz(a):
    c,s=math.cos(a),math.sin(a); return [[c,-s,0],[s,c,0],[0,0,1]]
def T(M): return [[M[c][r] for c in range(3)] for r in range(3)]

# cameras: 450 mm from board centre, 35 deg above the plane, at three azimuths
cams={}
for i,az in enumerate([90.0, 210.0, 330.0]):
    d=450.0/MM; el=math.radians(35.0); a=math.radians(az)
    C=[d*math.cos(el)*math.cos(a), d*math.cos(el)*math.sin(a), d*math.sin(el)]
    # camera looks at the origin: build R with -C as the +Z (optical) axis
    z=[-C[0]/d,-C[1]/d,-C[2]/d]
    up=[0,0,1]
    x=[up[1]*z[2]-up[2]*z[1],up[2]*z[0]-up[0]*z[2],up[0]*z[1]-up[1]*z[0]]
    n=math.sqrt(sum(v*v for v in x)); x=[v/n for v in x]
    y=[z[1]*x[2]-z[2]*x[1],z[2]*x[0]-z[0]*x[2],z[0]*x[1]-z[1]*x[0]]
    R=[[x[0],y[0],z[0]],[x[1],y[1],z[1]],[x[2],y[2],z[2]]]
    R=T(R)  # world->cam rows are the axes
    t=[-sum(R[r][k]*C[k] for k in range(3)) for r in range(3)]
    H=[[0]*3 for _ in range(3)]
    for r in range(3):
        H[r][0]=R[r][0]; H[r][1]=R[r][1]; H[r][2]=t[r]
    H=mm(K,H)
    cams[i+1]={"H":H,"R":R,"t":t,"C":C}
    print("I1493CAM cam=%d sees=1 scorable=1 frameW=1280 frameH=720 doublesPx=300 doublesMinorPx=250 "
          "bull=640,360 conicOfDoubles=1 planeBuilt=1 tilt=0.29 tiltAngle=0 conicRadius=1 "
          "anchored=1 phaseDeg=0.000000 H=%s"
          % (i+1, ",".join("%.9f"%H[r][c] for r in range(3) for c in range(3))))

def project(cam, X):
    R,t=cam["R"],cam["t"]
    p=[sum(R[r][k]*X[k] for k in range(3))+t[r] for r in range(3)]
    q=matvec(K,p)
    return q[0]/q[2], q[1]/q[2]

# dart 1: a point ON the board plane, seen by all three -> residual must be ~0
# dart 2: a point 20 mm PROUD of the board, seen by all three -> residual ~0, height +20
for d,X in [(1,[40.0/MM, -20.0/MM, 0.0]), (2,[40.0/MM,-20.0/MM,20.0/MM])]:
    for c in (1,2,3):
        px,py=project(cams[c],X)
        print("I1493DART dart=%d cycle=%d state=SINGLE_DART cam=%d frame=1 tipFound=1 tip=%.6f,%.6f "
              "score=S1 onBoard=1 bpR=0.3" % (d,d*10,c,px,py))
print("I1493END cycles=100 darts=2")
