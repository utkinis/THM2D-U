function [Pf,T] = load_mufits(filepath,sz)
    fid = fopen(filepath,'rb');
    Pf  = fliplr(fread(fid,sz,'double'));
    T   = fliplr(fread(fid,sz,'double'));
    fclose(fid);
end
