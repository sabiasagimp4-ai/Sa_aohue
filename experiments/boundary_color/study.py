"""Sa_aohue boundary-colour feasibility study (not a YMM renderer).

Original CPU prototype. Synthetic numerical fixtures only; no input photos,
image generation, trained model, UI changes or production shader changes.
Run: python experiments/boundary_color/study.py
Requires numpy and scipy. Writes reproducible metrics.json next to this file.
"""
from pathlib import Path
import json
import math
import sys
import numpy as np
from scipy.ndimage import gaussian_filter, gaussian_filter1d, distance_transform_edt, map_coordinates

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tests'))
from test_ymm_v02 import response as dog_response

# Same OKLab matrices as Composite.hlsl, with NumPy row vectors.
M1 = np.array([[.4122214708,.5363325363,.0514459929],
               [.2119034982,.6806995451,.1073969566],
               [.0883024619,.2817188376,.6299787005]])
M2 = np.array([[.2104542553,.793617785,-.0040720468],
               [1.9779984951,-2.428592205,.4505937099],
               [.0259040371,.7827717662,-.808675766]])
INV1 = np.array([[4.0767416621,-3.3077115913,.2309699292],
                 [-1.2684380046,2.6097574011,-.3413193965],
                 [-.0041960863,-.7034186147,1.707614701]])
INV2 = np.array([[1,.3963377774,.2158037573],
                 [1,-.1055613458,-.0638541728],
                 [1,-.0894841775,-1.291485548]])


def from_lab(lab):
    return ((lab @ INV2.T) ** 3) @ INV1.T


def to_lab(rgb):
    return np.cbrt(rgb @ M1.T) @ M2.T


def gamut_map(lab):
    """Hold L, reduce chroma when needed, as in the production approach."""
    out = lab.copy()
    rgb = from_lab(out)
    invalid = np.any((rgb < 0) | (rgb > 1), axis=-1)
    lo = np.where(invalid, 0., 1.)
    hi = np.ones_like(lo)
    for _ in range(16):
        mid = (lo + hi) / 2
        trial = lab.copy()
        trial[..., 1:] *= mid[..., None]
        candidate = from_lab(trial)
        ok = np.all((candidate >= 0) & (candidate <= 1), axis=-1)
        lo = np.where(ok, mid, lo)
        hi = np.where(ok, hi, mid)
    out[..., 1:] *= lo[..., None]
    return out


def sample(image, y, x):
    if image.ndim == 2:
        return map_coordinates(image, [y, x], order=1, mode='nearest')
    return np.stack([sample(image[..., c], y, x) for c in range(image.shape[-1])], axis=-1)


def boundary_data(lab, color_edges=True):
    """Local colour tensor -> normal, thin ridge seeds, paired side samples."""
    smooth = gaussian_filter(lab, (.8, .8, 0), mode='nearest')
    gy, gx = np.gradient(smooth, axis=(0, 1))
    if not color_edges:
        gy, gx = gy[..., :1], gx[..., :1]
    xx = np.sum(gx*gx, axis=-1)
    yy = np.sum(gy*gy, axis=-1)
    xy = np.sum(gx*gy, axis=-1)
    angle = .5 * np.arctan2(2*xy, xx-yy)
    nx, ny = np.cos(angle), np.sin(angle)
    gap = np.sqrt((xx-yy)**2 + 4*xy*xy)
    energy = np.sqrt(np.maximum(0, (xx+yy+gap)/2))
    coherence = gap / np.maximum(xx+yy, 1e-12)
    y, x = np.indices(energy.shape)
    ridge = ((energy >= sample(energy, y+ny, x+nx)) &
             (energy >= sample(energy, y-ny, x-nx)))
    seeds = ridge & (energy > .015)
    # Float fields remain independent of a particular orientation/sign of n.
    pair = sample(smooth, y+3*ny, x+3*nx) - sample(smooth, y-3*ny, x-3*nx)
    return seeds, nx, ny, pair, energy, coherence


def exchange(lab, amount=.65, radius=12., color_edges=True, flip_normals=False):
    """Spread each boundary's opposing chroma difference to nearby pixels.

    Exact CPU Euclidean nearest seed, not JFA and not the Diffusion Curves PDE.
    Positive amount borrows the other side's colour; negative repels from it.
    L is unchanged. No alpha support or temporal correspondence is claimed.
    """
    seeds, nx, ny, pair, energy, coherence = boundary_data(lab, color_edges)
    if not np.any(seeds):
        return lab.copy(), np.zeros(lab.shape[:2]), seeds
    distance, nearest = distance_transform_edt(~seeds, return_indices=True)
    sy, sx = nearest
    y, x = np.indices(seeds.shape)
    normal_x, normal_y = nx[sy,sx], ny[sy,sx]
    difference = pair[sy,sx,1:]
    if flip_normals:
        normal_x, normal_y, difference = -normal_x, -normal_y, -difference
    signed = (x-sx)*normal_x + (y-sy)*normal_y
    weight = np.exp(-.5*(distance/radius)**2)
    weight *= np.clip(energy[sy,sx]/.08, 0, 1) * coherence[sy,sx]
    # Smooth signed side choice and zero at seed center. Avoid a sign ambiguity
    # on the exact contour; arbitrary tensor eigenvector sign must not matter.
    delta = -np.tanh(signed/.75)[...,None] * difference
    out = lab.copy()
    out[...,1:] += amount*weight[...,None]*delta
    return out, weight, seeds


def line_spread_study():
    x = np.arange(1025)-512
    rows = []
    for sigma in (6, 12, 24, 48):
        seed = (x == 0).astype(float)
        blur = gaussian_filter1d(seed, sigma, truncate=3)
        distance_mask = np.exp(-.5*(np.abs(x)/sigma)**2)
        rows.append(dict(sigma=sigma, gaussian_peak=float(blur.max()),
                         distance_peak=float(distance_mask.max()),
                         chroma_gain_at_amount_100=1+float(blur.max())))
    thick = (np.abs(x) <= 2.5).astype(float)
    # Five-pixel seed band vs a one-pixel seed, same range.
    ratio = gaussian_filter1d(thick,24,truncate=3).max()/rows[2]['gaussian_peak']
    return dict(model='Ideal normalized Gaussian, not Direct2D output',
                single_pixel_line=rows, five_vs_one_pixel_peak_ratio=float(ratio))


def color_study():
    h,w=65,161
    image=np.zeros((h,w,3));image[...,0]=.72
    image[:,w//2:,1:]=[-.025,-.065]
    positive,weight,seeds=exchange(image)
    negative,_,_=exchange(image,amount=-.65)
    y,x=h//2,w//2-4
    neutral=image[y,x,1:]
    original_chroma=np.linalg.norm(neutral)
    # Current relative chroma and hue rotation cannot create chroma from zero.
    max_current=np.linalg.norm(neutral*5)
    borrowed=np.linalg.norm(positive[y,x,1:])
    donor=image[y,w//2+8,1:]
    assert original_chroma == max_current == 0
    assert borrowed > .005
    assert np.dot(positive[y,x,1:],donor) > 0
    assert np.dot(negative[y,x,1:],donor) < 0
    assert np.max(np.abs(positive[...,0]-image[...,0])) == 0
    flipped,_,_=exchange(image,flip_normals=True)
    assert np.max(np.abs(flipped-positive)) < 1e-12
    mapped=gamut_map(positive)
    rgb=from_lab(mapped)
    assert np.min(rgb)>-1e-10 and np.max(rgb)<1+1e-10
    assert np.max(np.abs(to_lab(rgb)[...,0]-image[...,0])) < 1e-6
    return dict(fixture='Same L; neutral left / blue right; opaque; 65x161',
                probe_x=x,probe_y=y,seed_count=int(seeds.sum()),
                current_C_at_400_percent=float(max_current),
                borrow_C=float(borrowed),
                borrow_ab=positive[y,x,1:].tolist(),
                repel_ab=negative[y,x,1:].tolist(),
                max_L_change_before_output=float(np.max(np.abs(positive[...,0]-image[...,0]))),
                max_L_roundtrip_error=float(np.max(np.abs(to_lab(rgb)[...,0]-image[...,0]))),
                normal_sign_flip_error=float(np.max(np.abs(flipped-positive))))


def chromatic_boundary_study():
    h,w=65,161
    image=np.zeros((h,w,3));image[...,0]=.68
    image[:,:w//2,1:]=[-.055,.015]
    image[:,w//2:,1:]=[.055,.015]
    rgb=from_lab(image)
    assert np.min(rgb)>0 and np.max(rgb)<1
    # Include a real RGB->Lab roundtrip, to rule out a synthetic exact-zero trick.
    roundtrip=to_lab(rgb)
    d=dog_response(roundtrip[...,0],np.ones((h,w)))
    threshold=128/255*.08
    old=int(np.count_nonzero(np.abs(d)>threshold))
    new=int(boundary_data(roundtrip)[0].sum())
    assert old==0 and new>0
    return dict(max_L_difference=float(np.ptp(roundtrip[...,0])),
                max_abs_production_reference_DoG=float(np.max(np.abs(d))),
                cutoff=threshold,detected_by_either_DoG_polarity=old,
                detected_by_colour_tensor=new)


def nearest_switch_failure():
    # A target lies between two boundaries carrying opposing colour changes.
    # The exact nearest-seed map has an unavoidable donor identity jump.
    x=np.array([-.01,.01]);seed_x=np.array([-8.,8.])
    delta=np.array([[-.05,0],[.05,0]])
    distances=np.abs(x[:,None]-seed_x[None,:])
    nearest=np.argmin(distances,axis=1)
    hard=delta[nearest]*np.exp(-.5*(distances[np.arange(2),nearest]/12)**2)[:,None]
    weights=np.exp(-.5*(distances/12)**2)
    soft=(weights@delta)/weights.sum(axis=1)[:,None]
    hard_jump=float(np.linalg.norm(hard[1]-hard[0]))
    soft_jump=float(np.linalg.norm(soft[1]-soft[0]))
    assert hard_jump>.05 and soft_jump<.001
    return dict(probe_movement_px=.02,hard_nearest_delta_ab_jump=hard_jump,
                two_candidate_mix_delta_ab_jump=soft_jump,
                caveat='Two-candidate mixing is an isolated alternative, not in exchange(); it can blend across unrelated boundaries.')


def translation_study():
    h,w=73,149
    def fixture(shift):
        y,x=np.indices((h,w));lab=np.zeros((h,w,3));lab[...,0]=.68
        disk=(x-(w//2+shift))**2+(y-h//2)**2 < 18**2
        lab[disk,0]=.77;lab[disk,1]=-.025;lab[disk,2]=-.05
        return lab
    a,_,_=exchange(fixture(0),radius=6)
    b,_,_=exchange(fixture(1),radius=6)
    error=np.max(np.abs(a[:,8:-9]-b[:,9:-8]))
    assert error<1e-12
    return dict(integer_shift=1,max_aligned_error=float(error),
                caveat='Integer translation only. Not subpixel temporal stability or optical flow.')


def enclosure_study():
    """2D opposing-ray evidence, NOT reconstructed 3D cavity or physical AO."""
    h,w=97,97;y,x=np.indices((h,w));cx,cy=w//2,h//2
    shapes={
        'single_edge': (x==cx-8),
        'parallel_gap': (x==cx-8)|(x==cx+8),
        'closed_square': (((np.abs(x-cx)==8)&(np.abs(y-cy)<=8)) |
                          ((np.abs(y-cy)==8)&(np.abs(x-cx)<=8))),
        'flat_stripe_texture': ((x-cx+8)%16==0),
    }
    result={}
    theta=np.arange(32)*2*np.pi/32
    t=np.arange(1,33,dtype=float)
    for name,mask in shapes.items():
        soft=gaussian_filter(mask.astype(float),.6,mode='constant')
        soft/=max(soft.max(),1e-12)
        rays=sample(soft,cy+np.sin(theta)[:,None]*t,cx+np.cos(theta)[:,None]*t)
        directional=np.max(rays*np.exp(-t/24),axis=1)
        paired=np.minimum(directional[:16],directional[16:])
        result[name]=dict(opposing_ray_score=float(np.mean(paired)),
                          gaussian_density=float(gaussian_filter(mask.astype(float),12)[cy,cx]))
    assert result['single_edge']['opposing_ray_score'] < 1e-6
    assert result['parallel_gap']['opposing_ray_score'] > .25
    assert result['closed_square']['opposing_ray_score'] > result['parallel_gap']['opposing_ray_score']
    assert result['flat_stripe_texture']['opposing_ray_score'] >= result['parallel_gap']['opposing_ray_score']-.001
    return dict(cases=result,
                cost='32 directions x 32 distances per evaluated point in this CPU reference; no GPU timing',
                limitation='Flat stripes also look enclosed. This is 2D line arrangement, not proof of a physical recess.')


def main():
    data=dict(baseline_commit='e5aaf011b029613ea33fce82d812683db7a5ab6e',
              implementation='CPU mathematical prototype; no YMM/GPU performance claim',
              spread=line_spread_study(),colour_exchange=color_study(),
              isoluminant_boundary=chromatic_boundary_study(),
              nearest_switch_failure=nearest_switch_failure(),
              translation=translation_study(),
              enclosure=enclosure_study())
    path=Path(__file__).with_name('metrics.json')
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(data,ensure_ascii=False,indent=2))


if __name__=='__main__':
    main()
