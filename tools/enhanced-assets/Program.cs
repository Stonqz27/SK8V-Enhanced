using CodeWalker.GameFiles;
using System.Security.Cryptography;
using System.Text.Json;
using System.Xml.Linq;

// Preparation only. Writes are restricted to this port's private local directory.
// No game memory writes, installation, or alteration of existing DLC archives.
try {
var workspace = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../../"));
var local = Path.Combine(workspace, "local");
string Destination(string path) {
    var full = Path.GetFullPath(path);
    if (!full.StartsWith(local + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
        throw new Exception("Output must be under the isolated port's local directory");
    if (File.Exists(full)) throw new Exception("Preserve the existing output before replacing it: " + full);
    Directory.CreateDirectory(Path.GetDirectoryName(full)!);
    return full;
}
string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
void Report(string path, object result) {
    var text = JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true });
    File.WriteAllText(Destination(path), text + "\n");
    Console.WriteLine(text);
}
YdrFile Drawable(byte[] bytes, bool gen9) {
    RpfManager.IsGen9 = gen9;
    var file = new YdrFile(); file.Load(bytes);
    if (file.Drawable?.ShaderGroup == null) throw new Exception("Invalid drawable");
    return file;
}
string ModelSignature(YdrFile file) {
    var doc = XDocument.Parse(YdrXml.GetXml(file));
    var root = new XElement("VerifiedBoard");
    root.Add(new XElement(doc.Root!.Element("Skeleton")!));
    foreach (var model in doc.Root.Element("DrawableModelsHigh")!.Elements("Item")) {
        var m = new XElement("Model");
        foreach (var name in new[] { "HasSkin", "BoneIndex", "Geometries" })
            m.Add(new XElement(model.Element(name)!));
        root.Add(m);
    }
    return root.ToString(SaveOptions.DisableFormatting);
}
object[] TextureSignature(YdrFile file) => file.Drawable.ShaderGroup.TextureDictionary.Textures.data_items
    .Select(t => (object)new { t.Name, t.Width, t.Height, t.Levels, format=t.Format.ToString(),
        bytes=t.Data.FullData.Length, sha256=Hash(t.Data.FullData) }).ToArray();

if (args.Length == 0) throw new Exception("Commands: extract, skeletons, shader-reference, convert-board, inspect-clip, stage-pack, stage-update");
switch (args[0]) {
    case "stage-update": {
        if(args.Length!=3)throw new Exception("stage-update <Enhanced game> <local output update RPF>");
        var game=Path.GetFullPath(args[1]);var source=Path.Combine(game,@"mods\update\update.rpf");
        var output=Destination(args[2]);
        var originalInfo=new FileInfo(source);var length=originalInfo.Length;var modified=originalInfo.LastWriteTimeUtc;
        File.Copy(source,output);
        originalInfo.Refresh();
        if(originalInfo.Length!=length || originalInfo.LastWriteTimeUtc!=modified)throw new Exception("Source archive changed during copy");
        GTA5Keys.LoadFromPath(game,true);RpfManager.IsGen9=true;
        var archive=new RpfFile(output,"update.rpf");archive.ScanStructure(_=>{},message=>throw new Exception(message));
        if(archive.IsNGEncrypted || archive.IsAESEncrypted)throw new Exception("Existing mod archive must already use OPEN encryption");
        var entry=archive.AllEntries.OfType<RpfFileEntry>().Single(e=>e.Path.EndsWith(@"common\data\dlclist.xml",StringComparison.OrdinalIgnoreCase));
        var oldData=archive.ExtractFile(entry);var text=System.Text.Encoding.UTF8.GetString(oldData);
        var paths=XDocument.Parse(text).Root!.Element("Paths")!;
        if(paths.Elements().Any(e=>e.Value.Replace('\\','/').TrimEnd('/').Equals("dlcpacks:/skatev",StringComparison.OrdinalIgnoreCase)))
            throw new Exception("A skatev DLC entry already exists; review its ownership");
        if(text.IndexOf("</Paths>",StringComparison.Ordinal)!=text.LastIndexOf("</Paths>",StringComparison.Ordinal))throw new Exception("Ambiguous DLC paths");
        var newData=System.Text.Encoding.UTF8.GetBytes(text.Replace("</Paths>","<Item>dlcpacks:/skatev/</Item></Paths>",StringComparison.Ordinal));
        var beforeOffset=(long)entry.FileOffset*512;var beforeSize=((entry.GetFileSize()+511)/512)*512;
        var oldItems=paths.Elements().Select(e=>e.ToString(SaveOptions.DisableFormatting)).ToArray();
        RpfFile.CreateFile(entry.Parent,entry.Name,newData);
        var check=new RpfFile(output,"update.rpf");check.ScanStructure(_=>{},message=>throw new Exception(message));
        var added=check.AllEntries.OfType<RpfFileEntry>().Single(e=>e.Path.EndsWith(@"common\data\dlclist.xml",StringComparison.OrdinalIgnoreCase));
        if(!check.ExtractFile(added).SequenceEqual(newData))throw new Exception("DLC list payload changed after writing");
        var newItems=XDocument.Parse(System.Text.Encoding.UTF8.GetString(newData)).Root!.Element("Paths")!.Elements().ToArray();
        if(newItems.Length!=oldItems.Length+1 || !newItems.Take(oldItems.Length).Select(e=>e.ToString(SaveOptions.DisableFormatting)).SequenceEqual(oldItems))
            throw new Exception("Existing DLC list changed");
        var afterOffset=(long)added.FileOffset*512;var afterSize=((added.GetFileSize()+511)/512)*512;
        var headerSize=(16+Math.Max(archive.EntryCount,check.EntryCount)*16L+Math.Max(archive.NamesLength,check.NamesLength)+511)/512*512;
        bool Allowed(long offset)=>offset<headerSize || (offset>=beforeOffset && offset<beforeOffset+beforeSize) || (offset>=afterOffset && offset<afterOffset+afterSize);
        // Compare the entire multi-gigabyte archive with fixed-size buffers. Any
        // byte change outside the RPF directory/names or this one payload fails.
        using var a=File.OpenRead(source);using var b=File.OpenRead(output);
        using var sourceHash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        using var outputHash=IncrementalHash.CreateHash(HashAlgorithmName.SHA256);
        var aa=new byte[1024*1024];var bb=new byte[1024*1024];long position=0,changed=0;
        for(;;) {
            var na=a.Read(aa);var nb=b.Read(bb);
            if(na==0 && nb==0)break;
            sourceHash.AppendData(aa,0,na);outputHash.AppendData(bb,0,nb);
            if(na!=nb || !aa.AsSpan(0,na).SequenceEqual(bb.AsSpan(0,nb))) {
                for(int i=0;i<Math.Max(na,nb);i++) {
                    if(i<na && i<nb && aa[i]==bb[i])continue;
                    if(!Allowed(position+i))throw new Exception("Unrelated archive byte changed at "+(position+i));
                    changed++;
                }
            }
            position+=Math.Max(na,nb);
        }
        originalInfo.Refresh();
        if(originalInfo.Length!=length || originalInfo.LastWriteTimeUtc!=modified)throw new Exception("Source archive changed during verification");
        Report(output+".receipt.json",new {command="stage-update",source,source_sha256=Convert.ToHexString(sourceHash.GetHashAndReset()).ToLowerInvariant(),
            output_sha256=Convert.ToHexString(outputHash.GetHashAndReset()).ToLowerInvariant(),
            original_dlc_entries=oldItems.Length,added_dlc="dlcpacks:/skatev/",existing_dlc_entries_preserved=true,
            changed_bytes=changed,allowed_change_ranges=new[]{new[]{0L,headerSize},new[]{beforeOffset,beforeOffset+beforeSize},new[]{afterOffset,afterOffset+afterSize}},
            unrelated_archive_bytes_preserved=true,verification_buffer_bytes=aa.Length+bb.Length,game_modified=false});
        break;
    }
    case "stage-pack": {
        if(args.Length!=5)throw new Exception("stage-pack <upstream clip DLC> <Enhanced board YDR> <board YTYP> <local output RPF>");
        var output=Destination(args[4]);
        File.Copy(args[1],output);
        RpfManager.IsGen9=true;
        var archive=new RpfFile(output,"dlc.rpf");archive.ScanStructure(_=>{},message=>throw new Exception(message));
        var bank=archive.Children.Single(r=>r.NameLower=="skatev_anim.rpf");
        var drawable=File.ReadAllBytes(args[2]);var archetype=File.ReadAllBytes(args[3]);
        if((Drawable(drawable,true).RpfFileEntry as RpfResourceFileEntry)?.Version!=159)throw new Exception("Board is not Enhanced");
        var type=new YtypFile();RpfFile.LoadResourceFile(type,archetype,2);
        var definition=type.AllArchetypes.Single().BaseArchetypeDef;
        if(type.CMapTypes.name.Hash!=JenkHash.GenHash("skatev_board") || definition.name.Hash!=JenkHash.GenHash("skatev_board") || definition.flags!=0x20020)
            throw new Exception("Unexpected board archetype name or creation flags");
        RpfFile.CreateFile(bank.Root,"skatev_board.ydr",drawable);
        RpfFile.CreateFile(bank.Root,"skatev_board.ytyp",archetype);
        var content=archive.AllEntries.OfType<RpfFileEntry>().Single(e=>e.NameLower=="content.xml");
        var doc=XDocument.Parse(System.Text.Encoding.UTF8.GetString(archive.ExtractFile(content)));
        const string request="dlc_skatev:/%PLATFORM%/anim/skatev_board.ityp";
        doc.Root!.Element("dataFiles")!.Add(new XElement("Item",new XElement("filename",request),new XElement("fileType","DLC_ITYP_REQUEST"),
            new XElement("overlay",new XAttribute("value","false")),new XElement("disabled",new XAttribute("value","true")),
            new XElement("persistent",new XAttribute("value","false")),new XElement("contents","CONTENTS_PROPS")));
        doc.Root.Element("contentChangeSets")!.Element("Item")!.Element("filesToEnable")!.Add(new XElement("Item",request));
        RpfFile.CreateFile(content.Parent,"content.xml",System.Text.Encoding.UTF8.GetBytes(doc.ToString()));
        var check=new RpfFile(output,"dlc.rpf");check.ScanStructure(_=>{},message=>throw new Exception(message));
        IEnumerable<RpfFileEntry> Entries(RpfFile rpf)=>rpf.AllEntries.OfType<RpfFileEntry>().Concat((rpf.Children??new List<RpfFile>()).SelectMany(Entries));
        var all=Entries(check).ToArray();
        var payloads=new List<object>();
        foreach(var pair in new[]{("skatev_board.ydr",drawable),("skatev_board.ytyp",archetype),("skatev_live.ycd",File.ReadAllBytes(Path.Combine(Path.GetDirectoryName(args[1])!,"skatev_live.ycd")))}) {
            var entry=all.Single(e=>e.Name==pair.Item1);
            var expected=pair.Item2.ToArray();var resource=RpfFile.CreateResourceFileEntry(ref expected,0);expected=ResourceBuilder.Decompress(expected);
            var actual=entry.File.ExtractFile(entry);
            if(!actual.SequenceEqual(expected) || (entry as RpfResourceFileEntry)?.Version!=resource.Version)throw new Exception("Packed resource changed: "+pair.Item1);
            payloads.Add(new {name=pair.Item1,resource_version=resource.Version,decompressed_sha256=Hash(actual)});
        }
        foreach(var name in new[]{"weapons_skatev.meta","weaponanimations_skatev.meta"})
            if(!all.Any(e=>e.Name==name))throw new Exception("Pack missing upstream board weapon metadata: "+name);
        using var stream=File.OpenRead(output);
        Report(output+".receipt.json",new {command="stage-pack",archive_sha256=Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant(),
            payloads,archetype_request=request,archetype_flags=definition.flags,requires_openrpf=true,legacy_private_mount_hooks_used=false,
            installed=false,live_spawn_verified=false});
        break;
    }
    case "skeletons": {
        if(args.Length!=3)throw new Exception("skeletons <Enhanced game> <local ped-cache directory>");
        var game=Path.GetFullPath(args[1]);var output=Path.GetFullPath(args[2]);
        Destination(Path.Combine(output,"extraction-receipt.json"));
        if(Directory.EnumerateFileSystemEntries(output).Any())throw new Exception("Ped cache must be empty");
        GTA5Keys.LoadFromPath(game,true);RpfManager.IsGen9=true;
        var manager=new RpfManager { ExcludePaths=new[]{"mods\\","SK8V\\"}, BuildExtendedJenkIndex=false };
        var errors=new List<string>();
        manager.Init(game,true,_=>{},message=>errors.Add(message),false,false);
        if(errors.Count!=0)throw new Exception("Archive scan failed: "+string.Join("; ",errors.Take(3)));
        var entries=manager.EntryDict.Values.OfType<RpfFileEntry>()
            .Where(e=>e.NameLower.EndsWith(".yft") && System.Text.RegularExpressions.Regex.IsMatch(e.Path,@"peds[^\\/]*\.rpf[\\/]",System.Text.RegularExpressions.RegexOptions.IgnoreCase))
            .Distinct().OrderBy(e=>e.Path,StringComparer.Ordinal).ToArray();
        // Keep patch/DLC skeletons before base copies, with explicit deterministic provenance.
        var selected=entries.GroupBy(e=>Path.GetFileNameWithoutExtension(e.NameLower))
            .Select(g=>g.OrderBy(e=>e.Path.StartsWith("x64",StringComparison.OrdinalIgnoreCase))
                .ThenByDescending(e=>e.Path,StringComparer.Ordinal).First()).ToArray();
        var receipts=new List<object>();var tags=new HashSet<ushort>();
        foreach(var entry in selected) {
            var file=manager.GetFile<YftFile>(entry);
            var bones=file?.Fragment?.Drawable?.Skeleton?.Bones?.Items;
            if(bones==null || bones.Length==0 || bones.Length>4096)throw new Exception("Invalid ped skeleton: "+entry.Path);
            var data=bones.Select((b,i)=>new {name=b.Name,tag=b.Tag,parent=b.ParentIndex,
                t=new[]{b.Translation.X,b.Translation.Y,b.Translation.Z},
                r=new[]{b.Rotation.X,b.Rotation.Y,b.Rotation.Z,b.Rotation.W},
                s=new[]{b.Scale.X,b.Scale.Y,b.Scale.Z}}).ToArray();
            if(data.Select(b=>b.tag).Distinct().Count()!=data.Length)throw new Exception("Duplicate ped bone tag: "+entry.Path);
            for(int i=0;i<data.Length;i++) {
                var b=data[i];
                if(b.parent < -1 || b.parent>=i || b.t.Concat(b.r).Concat(b.s).Any(v=>!float.IsFinite(v)))
                    throw new Exception("Invalid parent or transform: "+entry.Path);
                tags.Add(b.tag);
            }
            var name=Path.GetFileNameWithoutExtension(entry.NameLower);
            var path=Destination(Path.Combine(output,name,"skeleton.json"));
            var text=JsonSerializer.Serialize(new {source=entry.Path,bones=data});File.WriteAllText(path,text);
            receipts.Add(new {ped=name,source=entry.Path,bones=data.Length,sha256=Hash(System.Text.Encoding.UTF8.GetBytes(text))});
        }
        foreach(var player in new[]{"player_zero","player_one","player_two"})
            if(!File.Exists(Path.Combine(output,player,"skeleton.json")))throw new Exception("Missing protagonist: "+player);
        Report(Path.Combine(output,"extraction-receipt.json"),new {command="skeletons",edition="Enhanced",source_entries=entries.Length,
            exported=selected.Length,unique_bone_tags=tags.Count,duplicate_policy="patch/DLC first; descending ordinal path",peds=receipts,game_modified=false});
        break;
    }
    case "extract": {
        if (args.Length != 5) throw new Exception("extract <Enhanced game> <archive relative path> <entry suffix> <output>");
        var game = Path.GetFullPath(args[1]);
        if (!File.Exists(Path.Combine(game, "GTA5_Enhanced.exe"))) throw new Exception("Expected Enhanced install");
        var archivePath = Path.GetFullPath(Path.Combine(game, args[2]));
        if (!archivePath.StartsWith(game + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase))
            throw new Exception("Archive outside game folder");
        var output = Destination(args[4]);
        GTA5Keys.LoadFromPath(game, true); RpfManager.IsGen9 = true;
        var archive = new RpfFile(archivePath, args[2]);
        archive.ScanStructure(_ => {}, message => throw new Exception(message));
        IEnumerable<RpfFileEntry> Entries(RpfFile rpf) => rpf.AllEntries.OfType<RpfFileEntry>()
            .Concat((rpf.Children ?? new List<RpfFile>()).SelectMany(Entries));
        var suffix = args[3].Replace('\\', '/').ToLowerInvariant();
        var found = Entries(archive).Where(e => e.Path.Replace('\\', '/').ToLowerInvariant().EndsWith(suffix)).ToArray();
        if (found.Length != 1) throw new Exception($"Expected exactly one entry, found {found.Length}: {suffix}");
        var entry = found[0];
        var bytes = entry.File.ExtractFile(entry);
        if (bytes == null || bytes.Length == 0 || bytes.Length > 64*1024*1024) throw new Exception("Invalid or oversized resource");
        if (entry is RpfResourceFileEntry resource)
            bytes = ResourceBuilder.AddResourceHeader(resource, ResourceBuilder.Compress(bytes));
        File.WriteAllBytes(output, bytes);
        Report(output + ".receipt.json", new { command="extract", source=archivePath, entry=entry.Path,
            resource_version=(entry as RpfResourceFileEntry)?.Version, bytes=bytes.Length, sha256=Hash(bytes), game_modified=false });
        break;
    }
    case "shader-reference": {
        if (args.Length != 3) throw new Exception("shader-reference <owned Enhanced YDR> <local output XML>");
        var source = File.ReadAllBytes(args[1]); var file = Drawable(source, true);
        var output = Destination(args[2]);
        // Use the reader's semantic XML, without attempting an unsupported
        // Gen9-to-Legacy binary conversion. No source texture/model is rewritten.
        var xml=YdrXml.GetXml(file);
        File.WriteAllText(output, xml);
        Report(output + ".receipt.json", new { command="shader-reference", source=Path.GetFullPath(args[1]),
            source_sha256=Hash(source), output_sha256=Hash(System.Text.Encoding.UTF8.GetBytes(xml)),
            source_version=159, game_modified=false });
        break;
    }
    case "convert-board": {
        if (args.Length != 3) throw new Exception("convert-board <prepared Legacy YDR> <local Enhanced YDR>");
        var source = File.ReadAllBytes(args[1]); var file = Drawable(source, false);
        var originalModel = ModelSignature(file); var originalTextures = TextureSignature(file);
        var skeleton = file.Drawable.Skeleton;
        if (skeleton?.Bones?.Items?.Length != 7) throw new Exception("Expected seven board bones");
        var bones = skeleton.Bones.Items;
        var names = new[] {"SKATEBOARD_ROOT","TRUCK_FRONT","TRUCK_BACK","LEFT_WHEELFRONT","LEFT_WHEELBACK","RIGHT_WHEELFRONT","RIGHT_WHEELBACK"};
        var parents = new[] {-1,0,0,1,2,1,2};
        for (int i=0;i<7;i++)
            if (bones[i].Name!=names[i] || bones[i].Tag!=i || bones[i].ParentIndex!=parents[i])
                throw new Exception("Board skeleton differs from runtime contract");
        RpfManager.IsGen9 = true;
        var bytes = file.Save(); var check = Drawable(bytes, true);
        var version = (check.RpfFileEntry as RpfResourceFileEntry)?.Version;
        if (version != 159) throw new Exception("Expected Enhanced YDR version 159");
        if (ModelSignature(check) != originalModel) throw new Exception("Bone/vertex/index data changed during Gen9 conversion");
        if (JsonSerializer.Serialize(TextureSignature(check)) != JsonSerializer.Serialize(originalTextures))
            throw new Exception("Texture pixels, format or mip levels changed during Gen9 conversion");
        if (check.Drawable.ShaderGroup.Shaders.data_items.Length != 3 || originalTextures.Length != 9)
            throw new Exception("Expected three board materials and nine textures");
        var output = Destination(args[2]); File.WriteAllBytes(output, bytes);
        File.WriteAllText(Destination(output + ".xml"), YdrXml.GetXml(check));
        Report(output + ".receipt.json", new { command="convert-board", source_sha256=Hash(source),
            output_sha256=Hash(bytes), resource_version=version, bones=names, parents,
            bone_vertex_index_data_preserved=true, texture_pixels_and_mips_preserved=true,
            textures=originalTextures, live_spawn_verified=false, game_modified=false });
        break;
    }
    case "inspect-clip": {
        if (args.Length != 3) throw new Exception("inspect-clip <YCD> <local evidence JSON>");
        RpfManager.IsGen9=true;
        var bytes=File.ReadAllBytes(args[1]);var ycd=new YcdFile();RpfFile.LoadResourceFile(ycd,bytes,46);
        if (ycd.ClipMap?.Count!=1 || ycd.AnimMap?.Count!=1) throw new Exception("Expected one live clip and animation");
        var animation=ycd.AnimMap.Single().Value.Animation;
        if (animation.Frames!=2) throw new Exception("Expected two live frames");
        var ids=animation.BoneIds.data_items;var sequences=animation.Sequences.data_items;
        if (sequences.Length!=1 || sequences[0].Sequences.Length!=ids.Length) throw new Exception("Live channel/track mismatch");
        var marker=Array.FindIndex(ids,b=>b.BoneId==65021 && b.Track==0);
        if(marker<0)throw new Exception("Missing live scan marker");
        var a=sequences[0].Sequences[marker].EvaluateVector(0);var b=sequences[0].Sequences[marker].EvaluateVector(1);
        if(a.X!=31337.25f || b.X!=-31337.75f)throw new Exception("Unexpected live scan marker");
        Report(args[2],new { command="inspect-clip", sha256=Hash(bytes), resource_version=(ycd.RpfFileEntry as RpfResourceFileEntry)?.Version,
            frames=animation.Frames, tracks=ids.Length, marker_frames=new[]{new[]{a.X,a.Y,a.Z},new[]{b.X,b.Y,b.Z}},
            enhanced_parser_verified=true, live_playback_verified=false });
        break;
    }
    default: throw new Exception("Unknown command");
}
return 0;
} catch(Exception error) {
    Console.Error.WriteLine("Enhanced asset preparation rejected: " + error.Message);
    return 2;
}
