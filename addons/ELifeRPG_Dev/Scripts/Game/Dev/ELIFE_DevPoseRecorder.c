//------------------------------------------------------------------------------------------------
//! Dev only: writes every bone's model-space matrix of a character to $profile:elife_pose_<name>.txt, 30 times a
//! second for a while, so animations can be read off exactly. One line per bone and frame:
//! "<frame> <seconds> <bone> <m0> <m1> <m2> <m3>" (four vectors, the rows of the matrix).
class ELIFE_DevPoseRecorder
{
	protected static const int INTERVAL_MS = 33;

	protected IEntity m_Entity;
	protected ref FileHandle m_File;
	protected ref array<string> m_aBones = {};
	protected int m_iFrame;
	protected int m_iFrames;
	protected int m_iStartTick;

	//------------------------------------------------------------------------------------------------
	static ELIFE_DevPoseRecorder Start(notnull IEntity entity, string name, float seconds)
	{
		ELIFE_DevPoseRecorder recorder = new ELIFE_DevPoseRecorder();
		if (!recorder.Open(entity, name, seconds))
			return null;

		return recorder;
	}

	//------------------------------------------------------------------------------------------------
	protected bool Open(IEntity entity, string name, float seconds)
	{
		Animation anim = entity.GetAnimation();
		if (!anim)
			return false;

		string path = "$profile:elife_pose_" + name + ".txt";
		m_File = FileIO.OpenFile(path, FileMode.WRITE);
		if (!m_File)
		{
			Print("ELIFE_DevPoseRecorder | can't write " + path, LogLevel.WARNING);
			return false;
		}

		m_Entity = entity;
		anim.GetBoneNames(m_aBones);
		m_iFrames = (int)(seconds * 1000 / INTERVAL_MS);
		m_iStartTick = System.GetTickCount();

		vector world[4];
		entity.GetWorldTransform(world);
		m_File.WriteLine(string.Format("# bones %1 world %2 %3 %4 %5", m_aBones.Count(), world[0], world[1], world[2], world[3]));
		Print("ELIFE_DevPoseRecorder | recording " + path);

		Sample();
		GetGame().GetCallqueue().CallLater(Sample, INTERVAL_MS, true);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected void Sample()
	{
		Animation anim;
		if (m_Entity)
			anim = m_Entity.GetAnimation();

		if (!anim || !m_File || m_iFrame >= m_iFrames)
		{
			Close();
			return;
		}

		float seconds = (System.GetTickCount() - m_iStartTick) / 1000.0;
		vector mat[4];
		foreach (string bone : m_aBones)
		{
			if (!anim.GetBoneMatrix(anim.GetBoneIndex(bone), mat))
				continue;

			m_File.WriteLine(string.Format("%1 %2 %3 %4 %5 %6 %7", m_iFrame, seconds, bone, mat[0], mat[1], mat[2], mat[3]));
		}

		m_iFrame++;
	}

	//------------------------------------------------------------------------------------------------
	protected void Close()
	{
		GetGame().GetCallqueue().Remove(Sample);
		if (m_File)
		{
			m_File.Close();
			m_File = null;
			Print(string.Format("ELIFE_DevPoseRecorder | done, %1 frames", m_iFrame));
		}
	}
}
