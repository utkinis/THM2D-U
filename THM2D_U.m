clear,figure(1),clf,colormap jet
%% Physical parameters
Lr         = 10000;                                           % Domain width                      [m]
Lz         = 1500;                                            % Domain height                     [m]
Or         = 0;                                               % Min r                             [m]
Oz         = -Lz;                                             % Min z                             [m]
kd0        = 5e9;                                             % Drained bulk modulus              [Pa]
ks0        = 30e9;                                            % Bulk modulus of the solid grains  [Pa]
mu0        = 2e9;                                             % Shear modulus of the solid grains [Pa]
alpha      = 1e-5;                                            % Thermal expansion coefficient     [1/K]
%% Numerical parameters
mfnr       = 200;                                             % Number of cells in r direction for unextended grid
mfnz       = 30;                                              % Number of cells in z direction for unextended grid
itref      = 10;                                              % Reference state time step
itstart    = 11;                                              % First time step
itend      = 132;                                             % Last time step
reltol     = 1e-8;                                            % Convergence criterion of pseudo-transient iterations
dmp        = 2;                                               % Damping parameter for pseudo-transient iterations
rincr      = 1.025;                                           % dr increment (refined grid)
zincr      = 1.05;                                            % dz increment (refined grid)
%% Coupling and output parameters
simdir     = 'input';                                         % Path to the directory containing .dat files converted from .SUM
simname    = 'CAMPI-FLEGREI-2D';                              % Name of the MUFITS simulation
outdir     = 'output';                                        % Path to the directory where the output files will be stored
%% Preprocessing
mfrvs      = refined_grid(Or,Lr,mfnr+1,rincr);                % R nodal coordinates
mfzvs      = Oz-flip(refined_grid(Oz,Lz,mfnz+1,zincr));       % Z nodal coordinates
drexp      = Lr/2;                                            % Grid extension cell spacing in r direction
dzexp      = Lz/2;                                            % Grid extension cell spacing in z direction
rvs        = [mfrvs Or+Lr+drexp:drexp:Or+3*Lr];               % R nodal center coordinates for extended grid
zvs        = [Oz-2*Lz:dzexp:Oz-dzexp mfzvs];                  % Z nodal center coordinates for extended grid
nr         = length(rvs)-1;                                   % Total number of cells in r direction
nz         = length(zvs)-1;                                   % Total number of cells in z direction
maxiter    = 500*max(nr,nz);                                  % Maximum number of pseudo-transient iterations
mfri       = 1:mfnr;                                          % Indices of cells in r direction for unextended grid
mfzi       = nz-mfnz+1:nz;                                    % Indices of cells in z direction for unextended grid
rcs        = 0.5*(rvs(1:end-1)+rvs(2:end));                   % R cell center coordinates
zcs        = 0.5*(zvs(1:end-1)+zvs(2:end));                   % Z cell center coordinates
drcs       = abs(rvs(2:end)-rvs(1:end-1));                    % Grid cell spacing in r direction
dzcs       = abs(zvs(2:end)-zvs(1:end-1));                    % Grid cell spacing in z direction
drvs       = abs(rcs(2:end)-rcs(1:end-1));                    % Grid nodal spacing in r direction
dzvs       = abs(zcs(2:end)-zcs(1:end-1));                    % Grid nodal spacing in z direction
drvsexp    = [drvs(1),drvs,drvs(end)];                        % Expanded r spacing for BCs
dzvsexp    = [dzvs(1),dzvs,dzvs(end)];                        % Expanded z spacing for BCs
[Rc,Zc]    = ndgrid(rcs,zcs);                                 % 2D cell centers grid
[Rr,Zr]    = ndgrid(rvs,zcs);                                 % 2D staggered grid in r direction
[Rz,Zz]    = ndgrid(rcs,zvs);                                 % 2D staggered grid in z direction
[Rrz,Zrz]  = ndgrid(rvs,zvs);                                 % Node centered staggered grid
Kd         = kd0*ones(nr,nz);                                 % Drained bulk modulus
Ks         = ks0*ones(nr,nz);                                 % Bulk modulus of the solid grains
Mu         = mu0*ones(nr,nz);                                 % Shear modulus of the solid grains
dtVs       = min(min(drcs),min(dzcs))  ...
            /max(sqrt(Kd(:)+4/3*Mu(:)))...
            /sqrt(2.1);                                       % Time step for pseudo-transient iterations
Biot       = 1 - Kd./Ks;                                      % Biot's coefficient
%% Init
Pt         = zeros(nr  ,nz  ); Pt0    = Pt;                   % Total pressure
Ur         = zeros(nr+1,nz  ); Ur0    = Ur;                   % Displacement in r direction
Uz         = zeros(nr  ,nz+1); Uz0    = Uz;                   % Displacement in z direction
Taurr      = zeros(nr  ,nz  ); Taurr0 = Taurr;                % Stress deviator rr component
Tauzz      = zeros(nr  ,nz  ); Tauzz0 = Tauzz;                % Stress deviator zz component
Tautt      = zeros(nr  ,nz  ); Tautt0 = Tautt;                % Stress deviator tt component
Taurz      = zeros(nr+1,nz+1); Taurz0 = Taurz;                % Stress deviator rz component
Vr         = zeros(nr+1,nz  );                                % Velocity in r direction
Vz         = zeros(nr  ,nz+1);                                % Velocity in z direction
Mu_vrz     = zeros(nr+1,nz+1);                                % Node centered shear modulus
Uzcevol    = nan*ones(1,itend-itstart+1);                     % Vertical displacement at observation point
filepath   = sprintf('%s/%s.%04d.dat',simdir,simname,itref);  % File containing reference parameters        
Pf0        = zeros(nr,nz); Pf = Pf0;                          % Fluid pressure (loaded from external files)
T0         = zeros(nr,nz); T  = T0;                           % Temperature    (loaded from external files)
[Pf0(mfri,mfzi),T0(mfri,mfzi)] = load_mufits(filepath,[mfnr,mfnz]);                      
outfile = sprintf('%s/%s.grid.mat',outdir,simname);
save(outfile,'Rc','Zc','Rr','Zr','Rz','Zz','Rrz','Zrz');
outfile = sprintf('%s/%s.ref.mat',outdir,simname);
save(outfile,'Pf0','T0','Pt0','Ur0','Uz0','Kd','Mu','Ks');
it         = itref;
%% Action
while it <= itend
    %% Load fluid pressure and temperature from MUFITS
    filepath                     = sprintf('%s/%s.%04d.dat',simdir,simname,it);
    [Pf(mfri,mfzi),T(mfri,mfzi)] = load_mufits(filepath,[mfnr,mfnz]);
    Mui                          = griddedInterpolant(Rc,Zc,Mu,'linear');
    Mu_vrz                       = Mui(Rrz,Zrz);
    %% Pseudo-transient iterations
    for iter = 1:maxiter
        change1                  = Ur;
        change2                  = Uz;
        divU                     = diff(Rr.*Ur,1,1)./drcs'./Rc + diff(Uz,1,2)./dzcs;
        % Calculate total pressure
        Pt                       = Pt0-Kd.*divU+Biot.*(Pf-Pf0)+alpha*Kd.*(T-T0);
        Ur_c                     = 0.5*(Ur(1:end-1,:)+Ur(2:end,:));
        % Strain rate deviators
        Err                      = diff(Ur,1,1)./drcs'-divU/3;
        Ezz                      = diff(Uz,1,2)./dzcs -divU/3;
        Ett                      = Ur_c./Rc-divU/3;
        Erz                      = 0.5*(diff(Ur(2:end-1,:),1,2)./dzvs...
                                       +diff(Uz(:,2:end-1),1,1)./drvs');
        % Stress deviators
        Taurr                    = Taurr0+2*Mu.*Err;
        Tauzz                    = Tauzz0+2*Mu.*Ezz;
        Tautt                    = Tautt0+2*Mu.*Ett;
        Taurz(2:end-1,2:end-1)   = Taurz0(2:end-1,2:end-1)+2*Mu_vrz(2:end-1,2:end-1).*Erz;
        % Stresses
        Srr                      = -Pt+Taurr;
        Szz                      = -Pt+Tauzz;
        Stt                      = -Pt+Tautt;
        Stti                     = griddedInterpolant(Rc,Zc,Stt,'linear');
        Stt_rc                   = Stti(Rr,Zr);
        Rcexp                    = [Rc(1,:)-drvs(1);Rc;Rc(end,:)+drvs(end)];
        Srrexp                   = [Srr(1,:);Srr; Srr(end,:)]; % Rollers at left and right boundaries
        Szzexp                   = [Szz(:,1),Szz,-Szz(:,end)]; % Roller at bottom and traction-free at top
        % Residuals
        RVr                      = diff(Rcexp.*Srrexp,1,1)./drvsexp'./Rr+diff(Taurz,1,2)./dzcs-Stt_rc./Rr;
        RVz                      = diff(Szzexp,1,2)./dzvsexp+diff(Rrz.*Taurz,1,1)./drcs'./Rz;
        Vr                       = Vr*(1-dmp/nr)+dtVs*RVr;
        Vz                       = Vz*(1-dmp/nz)+dtVs*RVz;
        % Update displacements
        Ur                       = Ur+dtVs*Vr;
        Uz                       = Uz+dtVs*Vz;
        Ur([1,end],:)            = 0; % Remove singularity at symmetry axis
        Uz(:      ,1)            = 0;
        % Check convergence
        change1                  = change1 - Ur;
        change2                  = change2 - Uz;
        abschange1               = max(abs(change1(:)));
        abschange2               = max(abs(change2(:)));
        eps1                     = max(reltol,reltol*max(abs(Ur(:))));
        eps2                     = max(reltol,reltol*max(abs(Uz(:))));
        check(1)                 = abschange1 < eps1;
        check(2)                 = abschange2 < eps2;
        if all(check) && iter > 3,break,end
    end
    %% Set reference parameters
    if it == itref
        Pt0    = Pt;
        Ur0    = Ur;
        Uz0    = Uz;
        Taurr0 = Taurr;
        Tauzz0 = Tauzz;
        Tautt0 = Tautt;
        Taurz0 = Taurz;
        Ur     = 0*Ur;
        Uz     = 0*Uz;
        Vr     = 0*Vr;
        Vz     = 0*Vz;
        if itref < itstart
            it = itstart;
            continue
        end
    end
    %% Update observation point displacement
    Uzcevol(it-itstart+1) = Uz(1,end);
    %% Draw figures
    ttl{1} = ['# of time step: ',num2str(it)];
    ttl{2} = ['# of pseudo-transient iterations = ',num2str(iter)];
    sgtitle(ttl,'FontSize',14);
    subplot(421)      ;pcolor(Rr,Zr,Ur)    ;shading interp ;axis image;axis([Or Or+Lr Oz Oz+Lz]);title('Ur')        ;colorbar
    subplot(422)      ;pcolor(Rz,Zz,Uz)    ;shading interp ;axis image;axis([Or Or+Lr Oz Oz+Lz]);title('Uz')        ;colorbar
    subplot(423)      ;pcolor(Rc,Zc,Pf-Pf0);shading interp ;axis image;axis([Or Or+Lr Oz Oz+Lz]);title('\Delta P_f');colorbar
    subplot(424)      ;pcolor(Rc,Zc,T-T0)  ;shading interp ;axis image;axis([Or Or+Lr Oz Oz+Lz]);title('\Delta T')  ;colorbar
    subplot(4,2,[5 6]);pcolor(Rc,Zc,Pt)    ;shading faceted;axis image;axis([Or Or+Lr Oz Oz+Lz]);title('P_t')       ;colorbar
    subplot(427)      ;plot(1e-3*rcs,100*Uz(:,end),'r-',1e-3*rvs,100*Ur(:,end),'m-');xlim([0 Lr]);
    subplot(428)      ;plot(Uzcevol(1:5:end),'kx');
    drawnow
    %% Save fields
    outfile = sprintf('%s/%s.%04d.mat',outdir,simname,it);
    save(outfile,'Pf','T','Pt','Ur','Uz');
    it = it+1;
end
%% Postprocessing
outfile = sprintf('%s/%s.post.mat',outdir,simname);
save(outfile,'Uzcevol');

%% Generate grid with non-uniform spacing
function x = refined_grid(ox,lx,nx,incr)
    if incr == 1
        x = ox + linspace(0,1,nx)*lx;
    else
        x = ox + lx*(incr.^(0:nx-1)-1)/(incr^(nx-1)-1);
    end
end