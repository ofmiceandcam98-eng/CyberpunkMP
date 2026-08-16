// CharacterProfile.reds - Redscript helper for accessing Character Identity & Profile fields
//
// Maps the 5-tier identity hierarchy:
// Discord Account -> Player ID -> Character ID -> Character Profile -> Character Entity

public class CharacterProfileData extends IScriptable {
    public let characterId: String;
    public let name: String;
    public let isMale: Bool;
    public let level: Int32;
    public let health: Float;
    public let maxHealth: Float;
    public let money: Int64;
    public let occupation: String;
    public let lifepath: String;
    public let affiliation: String;
    public let bio: String;
    public let bioSet: Bool;
    public let isAffiliationLeader: Bool;
}